# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A local monitoring dashboard for Claude Code sessions. A FastAPI service on the Windows host
receives Claude Code hooks, folds them into per-session state, and serves that state to a
tablet page, a Windows tray icon, and (planned) an ESP32 AMOLED desk display.

Each host runs its own service. A device sees only the hosts it has paired with, so a
colleague's machine on the same LAN is discoverable but unreadable.

All UI text and comments are Chinese. Match that when editing.

## Commands

```powershell
pip install -r requirements-dev.txt     # 含 pytest / httpx / httpx2 / pyinstaller

python -m pytest tests/ -v              # 100 tests
python server.py                        # 0.0.0.0:8787，无托盘
python tray.py                          # 托盘 + 同一个服务，日常用这个
build.bat                               # -> dist\AgentDashboard\AgentDashboard.exe
```

`tray.py` is the packaged entry point and a strict superset of `server.py`.

Firmware: `agent_display.ino` is the pre-rewrite skeleton and **does not work against the
current server** — it polls `/api/tiny` without a token and its `BUZZER_PIN 2` collides with
this board's I2C SCL. It is replaced wholesale by the ESP32 plan (see Roadmap).

## Architecture

`server.py` is a thin assembly layer. The logic lives in seven focused modules, all of which
take an injectable `now` where time matters so the state machine is testable without sleeping.

| Module | Responsibility |
|---|---|
| `config.py` | Token + `host_id`, persisted to `%APPDATA%\AgentDashboard\config.json` |
| `phrases.py` | All Chinese copy — the single source of truth for every surface |
| `sessions.py` | `SESSIONS` state machine, transcript summary, redaction, `snapshot()` |
| `metrics.py` | System metrics collector thread |
| `security.py` | Token compare, loopback check, the 60-second pairing window |
| `tiny.py` | The three-line plain-text payload the ESP32 polls |
| `discovery.py` | `Broadcast` — mDNS registration that follows the host's IP |

**Never name a module `copy.py`** — it shadows the standard library.

### Endpoints

| Route | Auth | Origin | Notes |
|---|---|---|---|
| `GET /`, `/static/*` | none | any | Page shell; carries no session data |
| `GET /api/stream` | token | any | SSE, one frame/second, full detail |
| `GET /api/tiny` | token | any | Three lines; `?d=full` / `?d=tool` overrides config |
| `POST /api/pair` | none | any | Returns the token **only** inside the 60s window |
| `POST /api/pair/open` | none | loopback | Opens the window without a tray |
| `POST /hook/{event}` | none | loopback | `?kind=` supplies the Notification matcher |

`docs_url` / `redoc_url` / `openapi_url` are disabled deliberately — they sat outside the
spec's endpoint matrix and handed a LAN scanner the route map.

### Event flow

`hooks-snippet.json` maps Claude Code events to `POST /hook/{event}` on `127.0.0.1:8787`.
`sessions.apply_event` collapses each into one of four states:

| event | state | detail |
|---|---|---|
| `session-start` | idle | fixed phrase |
| `prompt` | running | fixed phrase |
| `tool` | running | `tool` + first of `command`/`file_path`/`pattern` |
| `notify` (permission) | **waiting** | hook `message` |
| `notify` (idle) | idle | hook `message` |
| `stop` | idle | transcript-tail summary |
| `session-end` | — | session removed |

Only post-hoc events are wired. `PreToolUse` is deliberately absent: it is blockable, so a
hung dashboard would slow the thing it monitors. The cost is that during a tool call the
display shows the previous tool for a few seconds. Hook handlers must return 200 fast.

`tool` and `arg` are stored **separately**, never pre-joined. Redaction is selective
re-joining, not after-the-fact regex — that is what lets `/api/tiny` emit a tool name with no
arguments and never emit `cwd` at all.

`snapshot()` is the single sorting authority: waiting → running → idle → stale, with
`running` sessions older than `IDLE_TIMEOUT` (90s) marked `stale`. `tiny.render_tiny` takes
`ss[0]` and relies on that; it does not re-sort, because two sort implementations would drift.

### Trust model

The token gates data; pairing distributes the token. Both entry points call
`server.start_background()`, which starts the metrics collector, the mDNS broadcast, and a
30-second thread that re-registers when the host's egress IP changes.

- Pairing: tray menu (or `POST /api/pair/open`) opens a 60-second window; each success fires
  `PAIRING.on_pair` — a console line by default, a tray balloon when the tray is running.
- mDNS TXT carries only `v` / `host` / `id`. Never add session data to it.
- Explicitly **not** defended (documented in the spec, don't "fix" silently): a stolen paired
  device, passive sniffing of plaintext HTTP, and other local processes reading the config.

## Design rules (fixed across all surfaces)

Changing one requires changing it everywhere.

- **Colour encodes state, never category.** run `#3FBFD8` / wait `#FFB020` / idle `#4A5B78`
  / stale `#E2564A`, duplicated in `tray.py:COLORS`, `static/index.html` CSS vars, and the
  firmware. Amber means "waiting on you" on every screen.
- **`waiting` is the only state allowed to shout.** If running also blinked, waiting would
  stop being noticeable. `permission_prompt` and `idle_prompt` must stay distinct — merging
  them turns the alert into crying wolf.
- **The top 6px bar is the core of the design**, meant to be read from 2m. It is deliberately
  *not* gated on the 叫醒我 click; only sound, vibration, and system notifications are, since
  those are what browsers require a gesture for.
- **Alert on transition, not on state.** The tray compares waiting-session **identities**, not
  counts — a count comparison silently swallows the alert when one session resolves as
  another arrives.
- **`detail` is "what it's doing now"**, not a summary. Only `Stop` swaps in summary text.
- **Disconnection must never render as normal.** 断了，正重连 / stale / the firmware's slow
  blue `NO LINK`.

Browser copy is duplicated in `static/index.html` because JS cannot import `phrases.py`;
`tests/test_page_copy.py` is the only thing preventing drift. Keep it passing.

## Gotchas

- `psutil.cpu_percent(interval=1.0)` **is** the collector's clock. Don't replace it with a
  non-blocking call plus `sleep`.
- `--noconsole` makes `sys.stdout` `None`; uvicorn's default logging config crashes on it.
  `log_config=None, access_log=False` in `tray.py` exists for that.
- uvicorn/zeroconf import dynamically — use `--collect-all`, not individual `--hidden-import`.
- `nvidia-ml-py` is the package, `pynvml` the import. `nvmlInit()` runs at import time, so a
  GPU-present code path cannot be unit-tested without restructuring.
- `starlette` 1.6 wants `httpx2` for `TestClient`; both `httpx` and `httpx2` are dev deps.
  Removing `httpx2` brings back a deprecation warning — it is not a typo.
- `TestClient` defaults `client` to `("testclient", 50000)`, which is **not** loopback and
  will 403 every `/hook/*` call. Pass `client=("127.0.0.1", 50000)`. Never widen
  `security.LOOPBACK` to satisfy a test harness.
- `.gitattributes` forces `*.bat text eol=crlf`. `cmd.exe` cannot parse `^` continuations
  with LF endings and fails in a way that looks like a corrupted command, not a line-ending
  problem. `core.autocrlf` is per-clone and cannot be relied on.
- `http://192.168.x.x` is not a secure context, so `wakeLock` and `Notification` are absent;
  the page degrades silently. Fix on the device or with an `mkcert` LAN cert.

## Known gaps

- `watch()` in `tray.py` has no automated test — it is coupled to real `sessions`/`metrics`/
  `time.sleep`. The identity-based balloon fix was verified by review only.
- The tray's five GUI behaviours (icon, tooltip, balloon, menu actions, autostart) have never
  been observed running; they need an interactive desktop.
- `metrics._collect_loop` catches exceptions per iteration, but a wedged `psutil` would still
  serve a stale sample with no staleness indicator on the page.

## Roadmap

`docs/superpowers/specs/2026-08-18-agent-dashboard-design.md` is the design authority.
`docs/superpowers/plans/2026-08-18-host-dashboard.md` is this (completed) plan. The ESP32
firmware is a separate plan, not yet written; it depends on `/api/tiny` and `/api/pair`,
targets T-Display-AMOLED-Lite (SH8501B0, 194×368, landscape 368×194), and must be built
against `LilyGo-Display-IDF`, which wants ESP-IDF 5.3.
