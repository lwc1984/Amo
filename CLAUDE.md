# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A local monitoring dashboard for Claude Code sessions. A FastAPI server on the Windows host
receives Claude Code HTTP hooks, folds them into per-session state, and streams that state
(plus system metrics) over SSE to four display surfaces: a browser dashboard (tablet), a
Windows tray icon, an ESP32 AMOLED desk display, and (planned) a phone push.

All UI text and comments are Chinese. Match that when editing.

## Commands

```powershell
pip install fastapi "uvicorn[standard]" psutil nvidia-ml-py   # server only
python server.py                                              # 0.0.0.0:8787

pip install pystray pillow                                    # + tray host
python tray.py                                                # tray icon, embeds server

build.bat                                                     # -> dist\AgentDashboard.exe
```

No tests, no linter. `agent_display.ino` is built in PlatformIO with
`board = lilygo-t-display-amoled-lite` and the LilyGo-AMOLED-Series library (bundles LVGL 8.x).

## Architecture

**Event flow.** `hooks-snippet.json` maps Claude Code events to `POST /hook/{event}` on
`127.0.0.1:8787`. `server.py:hook()` keys everything on `session_id` into the in-memory
`SESSIONS` dict and collapses each event into one of four states:

| event | state | detail |
|---|---|---|
| `session-start` | idle | fixed string |
| `prompt` | running | "正在思考" |
| `tool` | running | tool name + first of `command`/`file_path`/`pattern` |
| `notify` | waiting (permission) / idle | hook `message` |
| `stop` | idle | last assistant text scraped from the transcript |
| `session-end` | — | session removed |

Only post-hoc hook events are wired up. `PreToolUse` is deliberately absent: it is blockable,
so a hung dashboard would slow Claude Code itself. The accepted cost is that during a tool
call the display shows the *previous* tool for a few seconds. Hook handlers must return 200
fast — never add blocking work to `hook()`.

**Metrics.** A daemon thread (`_collect_loop`) writes into the module-global `_metrics`.
Its tick is `psutil.cpu_percent(interval=1.0)` — that blocking call *is* the loop timer,
so don't replace it with a non-blocking variant plus `sleep`. NVML is optional; a machine
with no NVIDIA GPU silently yields `None` and clients render `—` (intended behavior).
CPU temperature was explicitly dropped from scope: `psutil` can't read it on Windows and
LibreHardwareMonitor wasn't worth the dependency.

**Fan-out.** `snapshot()` is the single source of truth: it marks `running` sessions older
than `IDLE_TIMEOUT` (90s) as `stale`, sorts waiting→running→idle→stale, and returns
`{metrics, sessions, ts}`. `GET /api/stream` pushes it as SSE once a second; `tray.py`
calls `snapshot()` directly in-process rather than over HTTP. The sort order is load-bearing
for `/api/tiny`, which just takes the first entry.

**Tray host.** `tray.py` imports `server` and runs uvicorn in a thread, so it is a superset
of `python server.py`. Single-instance detection is a bind attempt on port 8787 — if taken,
it opens the browser and exits. Autostart is an HKCU `...\Run` value; the icon is drawn at
runtime with PIL, so there is no `.ico` to package.

## Design rules (fixed across all surfaces — don't break these)

These were decided deliberately; changing one requires changing it everywhere.

- **Color encodes state, never category.** run `#3FBFD8` / wait `#FFB020` / idle `#4A5B78`
  / stale `#E2564A`. Duplicated in `tray.py:COLORS`, the CSS vars in `index.html`, and the
  `C_RUN`/`C_WAIT` defines in `agent_display.ino`. Amber means "it's waiting on you" on
  every screen, so a user never re-learns the language.
- **`waiting` is the only state allowed to shout.** Everything else stays quiet. If running
  also blinked, waiting would stop being noticeable. `permission_prompt` (needs approval,
  alarm) and `idle_prompt` (finished, grey) must stay separated — merging them turns the
  alert into crying wolf.
- **The top color bar is the core of the design**, not decoration: 6px full-width on the
  web page, 3px on the ESP32. It is meant to be read with peripheral vision from 2m away.
- **`detail` is "what it is doing right now", not a summary.** Only after `Stop` does it
  become transcript-scraped summary text.
- **Disconnection must never render as normal.** Web shows 断开重连中, ESP32 shows a slow
  blue `NO LINK`, `snapshot()` marks sessions `stale`. A monitor that hides its own failure
  is worse than no monitor.
- **Alert on transition, not on state.** The ESP32 buzzer and the tray balloon fire once
  when entering `waiting`, never repeatedly while it persists.

**ESP32 specifics.** Brightness is a design element, not a setting: idle 20/255 (AMOLED
black pixels draw ~no power, so it reads as a black glass tile at night), running 60,
waiting 255 + breathing. Lit area — not icon detail — encodes state, because at 194px wide
icons are unreadable from across the room. `shift_task` nudges the screen 1px/minute
against burn-in.

## Known gaps

- `server.py` serves `static/index.html` and mounts `StaticFiles(directory=BASE/"static")`,
  but `index.html` sits at the repo root and no `static/` directory exists — the server
  fails to start as checked in. Intended fix (designed but never applied): create `static/`,
  move `index.html` into it, and add the PyInstaller-aware resolver, since `--noconsole`
  builds unpack resources to `sys._MEIPASS`:

  ```python
  def resource_path(rel: str) -> Path:
      return Path(getattr(sys, "_MEIPASS", Path(__file__).parent)) / rel
  STATIC = resource_path("static")   # replaces BASE / "static"
  ```

- `agent_display.ino` polls `GET /api/tiny`, which `server.py` does not implement. Designed
  shape — plain text, no JSON, so the MCU parses with one `sscanf` plus an `indexOf('\t')`:

  ```python
  @app.get("/api/tiny", response_class=PlainTextResponse)
  async def tiny():
      """waiting,running,idle|会话名\tdetail"""
      ss = snapshot()["sessions"]
      n = lambda st: sum(1 for s in ss if s["state"] == st)
      top = next((s for s in ss if s["state"] in ("waiting", "running")), None)
      return (f'{n("waiting")},{n("running")},{n("idle")}|'
              + (f'{top["name"]}\t{top.get("detail","")[:40]}' if top else ""))
  ```

- A landscape ESP32 layout (368×194, two sessions visible at once, metrics filling the
  idle-state right half) was designed but not implemented. It is a `setRotation(1)` plus a
  coordinate rewrite inside `set_state()`; the state machine and polling need no changes.
- Phone fallback push (ntfy.sh / Bark) — the fourth surface — is planned, not built. It
  belongs in the `waiting` branch of `hook()`.

## Gotchas

- `--noconsole` makes `sys.stdout` `None` and uvicorn's default logging config crashes on
  it. The `log_config=None, access_log=False` in `tray.py:main()` exists for that — keep it.
- uvicorn imports `uvicorn.loops.*` / `uvicorn.protocols.*` dynamically, invisible to
  PyInstaller's static analysis. Use `--collect-all uvicorn`, not individual
  `--hidden-import` lines.
- `nvidia-ml-py` is the package name, `pynvml` the import name. It binds `nvml.dll` from the
  GPU driver, not from the wheel.
- LVGL font sizes must be enabled individually in `lv_conf.h` (`LV_FONT_MONTSERRAT_48 1`);
  only 14 is compiled by default. An `undefined reference` on a font is config, not code.
- Chinese glyphs on the ESP32 need a subset font built with `lv_font_conv` — never the full
  CJK set. The current firmware sidesteps this by using English state words.
- The `--onefile` build unpacks to `%TEMP%` on every launch (2-3s cold start, occasional AV
  false positives). `--onedir` is the better tradeoff for something that starts once a day.
- Firewall: allow TCP 8787 on the **private** profile, or the tablet cannot connect.
- `http://192.168.x.x` is not a secure context, so `navigator.wakeLock` and `Notification`
  are simply absent; `index.html` degrades silently to sound + vibration + visuals. Fix it
  on the device (tablet developer options → stay awake) or with an `mkcert` LAN cert.

## Extension notes

- Multi-host: key `SESSIONS` by `f"{hostname}:{session_id}"`.
- `/hook/{event}` is a private protocol — any agent that can send webhooks reuses the whole
  UI unchanged.
