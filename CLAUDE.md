# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A local monitoring dashboard for Claude Code sessions. A FastAPI service on the Windows host
receives Claude Code hooks, folds them into per-session state, and serves that state to a
tablet page, a Windows tray icon, and an ESP32 desk display shaped like a monster's mouth.

Each host runs its own service. A device sees only the hosts it has paired with, so a
colleague's machine on the same LAN is discoverable but unreadable.

All UI text and comments are Chinese. Match that when editing.

## Commands

```powershell
pip install -r requirements-dev.txt     # 含 pytest / httpx / httpx2 / pyinstaller

python -m pytest tests/ -v              # 126 tests
python server.py                        # 0.0.0.0:8787，无托盘
python tray.py                          # 托盘 + 同一个服务，日常用这个
build.bat                               # -> dist\AgentDashboard\AgentDashboard.exe
```

Firmware (needs the IDF env plus w64devkit's gcc/make on PATH — see `amo-display/README.md`):

```powershell
cd amo-display\host_test ; make          # 76 assertions, pure logic, no hardware needed
cd amo-display ; idf.py build            # then: idf.py -p COM<n> flash
```

**After `idf.py flash`, unplug and replug the USB.** The board uses the chip's built-in
USB-Serial-JTAG; esptool's closing reset usually leaves it in download mode, which looks
exactly like a dead board — flashing succeeded but the serial port stays silent.

`tray.py` is the packaged entry point and a strict superset of `server.py`.

Firmware lives in `amo-display/` (ESP-IDF). `agent_display.ino` is the dead pre-rewrite
skeleton — it targets the wrong board entirely and is kept only as history.

The board is a **LilyGo T-Display S3** (ST7789 LCD 170×320 over an 8-bit i80 bus, no PMU,
no touch, no RGB LED), not the AMOLED model the early docs assumed. It is mounted inside
the mouth opening of a 3D-printed pixel-art Claude monster, so the UI is a **face**, not a
dashboard: five states, five mouth shapes. See spec §5.1 (hardware, verified on the
physical board) and §5.3 (the mouth).

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
`docs/superpowers/plans/2026-08-18-host-dashboard.md` is the (completed) host plan.
`docs/superpowers/plans/2026-08-18-esp32-display.md` is the ESP32 plan — **Tasks 0-9 are
done and its body is largely superseded**; the code is the authority for those, and each
deviation is recorded in the commit that made it. Its top banner tracks what still stands.

Remaining: Task 10 (cleanup), verifying `/api/pair` returns 403 once the window closes
(needs the board's NVS cleared, which needs a button press), and tablet QR pairing.
