# 宿主端控制台 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把仓库里起不来的服务骨架变成一个可启动、有鉴权、能被 ESP32 和平板消费的宿主端控制台。

**Architecture:** 把现在 200 行一把抓的 `server.py` 按职责拆成 7 个聚焦模块（配置、文案、会话状态机、指标采集、安全、MCU 端点渲染、服务发现），`server.py` 退化成薄装配层只负责挂端点。所有纯逻辑（状态机、脱敏、令牌校验、三行格式）都做成可注入时间、不依赖 FastAPI 的纯函数，因此能被 pytest 直接覆盖。

**Tech Stack:** Python 3.11、FastAPI、uvicorn、psutil、nvidia-ml-py(pynvml)、zeroconf、pystray、Pillow、pytest、httpx

**Spec:** `docs/superpowers/specs/2026-08-18-agent-dashboard-design.md`

## Global Constraints

以下取值直接抄自 spec，每个任务的要求都隐含包含本节。

- 监听端口 `8787`；`IDLE_TIMEOUT = 90` 秒
- 令牌 `secrets.token_hex(16)`（32 个十六进制字符），校验用 `secrets.compare_digest`
- `host_id = uuid4().hex[:8]`
- 配置文件 `%APPDATA%\AgentDashboard\config.json`
- 令牌通过 `?k=<token>` 或 `X-Agent-Key` 头提供
- `/api/*` 需令牌；`/hook/*` 仅接受回环来源（`127.0.0.1` / `::1`）；`/` 与 `/static/*` 免令牌
- `POST /api/pair` 仅在 60 秒配对窗口内返回令牌，窗口外 403，每次成功回调一次通知
- mDNS 服务类型 `_agentdash._tcp.local.`，TXT **只含** `v=1` / `host=<主机名>` / `id=<host_id>`
- `/api/tiny` 三行格式：`版本|w,r,i|主机名` / `会话名\tdetail` / `cpu,mem,gpu,net_kb`；无 GPU 时 gpu 为 `-1`；默认脱敏到工具名，`?d=full` 给完整参数
- `SESSIONS` 的 key 为 `f"{HOST}:{session_id}"`，`HOST = socket.gethostname()`
- 语义色不可更改：run `#3FBFD8` / wait `#FFB020` / idle `#4A5B78` / stale `#E2564A`
- 状态文案（长/短）：waiting `哥们儿，该你了`/`该你了`；running `干着呢，别催`/`干着呢`；idle `摸鱼中，等你发话`/`摸鱼中`；stale `没声儿了，人呢？`/`没声儿了`
- 所有面向用户的字符串一律中文口语，三端共用同一份常量
- 目标平台 Windows；路径处理一律用 `pathlib`，不写死盘符

## 文件结构

| 文件 | 职责 |
|---|---|
| `config.py` | 令牌与 host_id 的生成和持久化 |
| `phrases.py` | 三端统一文案常量（**不可命名为 `copy.py`**，会遮蔽标准库 `copy`） |
| `sessions.py` | `SESSIONS` 状态机、transcript 摘要、脱敏、`snapshot()` |
| `metrics.py` | 系统指标采集线程 |
| `security.py` | 令牌比对、来源判定、配对窗口 |
| `tiny.py` | `/api/tiny` 的三行文本渲染 |
| `discovery.py` | zeroconf 注册与注销 |
| `server.py` | FastAPI 装配与端点（薄） |
| `static/index.html` | 平板页面（由仓库根移入） |
| `tray.py` | 托盘宿主（改：配对菜单、带令牌 URL、气泡） |
| `tests/` | pytest 套件 |

---

### Task 0: 仓库基线与依赖

**Files:**
- Create: `.gitignore`, `requirements.txt`, `requirements-dev.txt`, `tests/__init__.py`, `tests/conftest.py`
- Test: `tests/test_smoke.py`

**Interfaces:**
- Consumes: 无
- Produces: 可运行的 `pytest`；`tests/conftest.py` 导出 fixture `tmp_config`（返回一个写在临时目录的 `config.Config`）与自动重置 `sessions.SESSIONS` 的 autouse fixture

> **注意：** 本目录当前不是 git 仓库。Step 1 会执行 `git init`。若不希望纳入版本控制，跳过 Step 1 与后续所有 Commit 步骤。

- [ ] **Step 1: 初始化仓库**

```bash
cd "D:/Lvwenchao/geek/Amo"
git init
git add -A
git commit -m "chore: 导入设计对话产出的骨架代码"
```

- [ ] **Step 2: 写 `.gitignore`**

```gitignore
__pycache__/
*.pyc
.pytest_cache/
build/
dist/
*.spec
managed_components/
sdkconfig
sdkconfig.old
amo-display/build/
```

- [ ] **Step 3: 写依赖清单**

`requirements.txt`：

```
fastapi
uvicorn[standard]
psutil
nvidia-ml-py
zeroconf
pystray
pillow
```

`requirements-dev.txt`：

```
-r requirements.txt
pytest
httpx
pyinstaller
```

- [ ] **Step 4: 装依赖**

Run: `python -m pip install -r requirements-dev.txt`
Expected: 安装成功。本机已有 `psutil` / `Pillow` / `pyinstaller`，其余为新增。

- [ ] **Step 5: 写 `tests/conftest.py`**

```python
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))


@pytest.fixture
def tmp_config(tmp_path):
    """一份写在临时目录、与真实 %APPDATA% 隔离的配置。"""
    import config
    return config.load_config(tmp_path / "config.json")


@pytest.fixture(autouse=True)
def clean_sessions():
    """每个用例都从空会话表开始，避免用例间互相污染。

    延迟到 fixture 内部再 import：Task 0 阶段 sessions.py 还不存在，
    顶层 import 会让整个套件收集失败。
    """
    try:
        import sessions
    except ModuleNotFoundError:
        yield
        return
    sessions.SESSIONS.clear()
    yield
    sessions.SESSIONS.clear()
```

`tests/__init__.py` 留空。

- [ ] **Step 6: 写冒烟测试**

`tests/test_smoke.py`：

```python
def test_pytest_runs():
    assert True
```

- [ ] **Step 7: 跑测试**

Run: `python -m pytest tests/test_smoke.py -v`
Expected: 1 passed（conftest 的 import 是延迟的，此时 `config.py` / `sessions.py` 还不存在也不会导致收集失败）

- [ ] **Step 8: Commit**

```bash
git add .gitignore requirements.txt requirements-dev.txt tests/
git commit -m "chore: 加入依赖清单与 pytest 骨架"
```

---

### Task 1: `config.py` —— 令牌与主机标识

**Files:**
- Create: `config.py`
- Test: `tests/test_config.py`

**Interfaces:**
- Consumes: 无
- Produces:
  - `config_dir() -> Path`
  - `@dataclass Config(token: str, host_id: str, tiny_detail: str = "tool")`
  - `load_config(path: Path | None = None) -> Config` —— 文件存在则读取，不存在则生成并落盘

- [ ] **Step 1: 写失败的测试**

`tests/test_config.py`：

```python
import json

import config


def test_generates_and_persists(tmp_path):
    p = tmp_path / "config.json"
    cfg = config.load_config(p)

    assert len(cfg.token) == 32
    assert int(cfg.token, 16) >= 0          # 必须是合法十六进制
    assert len(cfg.host_id) == 8
    assert cfg.tiny_detail == "tool"
    assert p.exists()


def test_reuses_existing(tmp_path):
    p = tmp_path / "config.json"
    first = config.load_config(p)
    second = config.load_config(p)

    assert first.token == second.token
    assert first.host_id == second.host_id


def test_tokens_differ_between_hosts(tmp_path):
    a = config.load_config(tmp_path / "a.json")
    b = config.load_config(tmp_path / "b.json")

    assert a.token != b.token
    assert a.host_id != b.host_id


def test_written_file_is_readable_json(tmp_path):
    p = tmp_path / "config.json"
    cfg = config.load_config(p)
    raw = json.loads(p.read_text("utf-8"))

    assert raw["token"] == cfg.token
    assert raw["host_id"] == cfg.host_id


def test_config_dir_under_appdata(monkeypatch, tmp_path):
    monkeypatch.setenv("APPDATA", str(tmp_path))
    assert config.config_dir() == tmp_path / "AgentDashboard"
```

- [ ] **Step 2: 跑测试确认失败**

Run: `python -m pytest tests/test_config.py -v`
Expected: FAIL —— `ModuleNotFoundError: No module named 'config'`

- [ ] **Step 3: 写实现**

`config.py`：

```python
"""令牌与主机标识的持久化。

令牌是设备能否读取本机会话数据的唯一凭据；host_id 让设备在本机 IP
变化之后仍能认出是同一台主机。
"""
import json
import os
import secrets
import uuid
from dataclasses import asdict, dataclass
from pathlib import Path

APP_DIR_NAME = "AgentDashboard"


def config_dir() -> Path:
    base = os.environ.get("APPDATA") or str(Path.home())
    return Path(base) / APP_DIR_NAME


@dataclass
class Config:
    token: str
    host_id: str
    tiny_detail: str = "tool"          # "tool" 脱敏到工具名 / "full" 给完整参数


def load_config(path: Path | None = None) -> Config:
    p = path or (config_dir() / "config.json")
    if p.exists():
        raw = json.loads(p.read_text("utf-8"))
        return Config(raw["token"], raw["host_id"], raw.get("tiny_detail", "tool"))

    cfg = Config(token=secrets.token_hex(16), host_id=uuid.uuid4().hex[:8])
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(json.dumps(asdict(cfg), indent=2), "utf-8")
    return cfg
```

- [ ] **Step 4: 跑测试确认通过**

Run: `python -m pytest tests/test_config.py -v`
Expected: 5 passed

- [ ] **Step 5: Commit**

```bash
git add config.py tests/test_config.py
git commit -m "feat: 令牌与 host_id 的生成和持久化"
```

---

### Task 2: `phrases.py` —— 三端统一文案

**Files:**
- Create: `phrases.py`
- Test: `tests/test_phrases.py`

**Interfaces:**
- Consumes: 无
- Produces: `STATE_LABEL: dict[str, str]`、`STATE_LABEL_SHORT: dict[str, str]`、以及下列字符串常量：`SESSION_START`、`THINKING`、`DONE`、`WAITING_DEFAULT`、`IDLE_DEFAULT`、`EMPTY_LIST`、`CONN_ON`、`CONN_OFF`、`ARM_BUTTON`、`ARMED_BUTTON`、`UNPAIRED`、`PAIRED_BALLOON`

- [ ] **Step 1: 写失败的测试**

`tests/test_phrases.py`：

```python
import phrases

STATES = ("waiting", "running", "idle", "stale")


def test_every_state_has_both_labels():
    for st in STATES:
        assert phrases.STATE_LABEL[st]
        assert phrases.STATE_LABEL_SHORT[st]


def test_short_labels_fit_narrow_screen():
    """ESP32 横屏状态词区宽度有限，短文案不得超过 4 个字。"""
    for st in STATES:
        assert len(phrases.STATE_LABEL_SHORT[st]) <= 4, st


def test_waiting_copy_matches_spec():
    assert phrases.STATE_LABEL["waiting"] == "哥们儿，该你了"
    assert phrases.STATE_LABEL_SHORT["waiting"] == "该你了"


def test_no_english_state_labels_remain():
    """告警文案必须是中文口语，不能残留骨架里的英文。"""
    for st in STATES:
        assert not phrases.STATE_LABEL[st].isascii()
```

- [ ] **Step 2: 跑测试确认失败**

Run: `python -m pytest tests/test_phrases.py -v`
Expected: FAIL —— `ModuleNotFoundError: No module named 'phrases'`

- [ ] **Step 3: 写实现**

`phrases.py`：

```python
"""三端统一文案。

颜色只编码状态、不编码分类；文案同理，一套走平板 / 托盘 / ESP32。
改这里等于同时改三块屏 —— 这正是我们要的。
"""

STATE_LABEL = {
    "waiting": "哥们儿，该你了",
    "running": "干着呢，别催",
    "idle": "摸鱼中，等你发话",
    "stale": "没声儿了，人呢？",
}

# ESP32 横屏状态词区窄，用短的
STATE_LABEL_SHORT = {
    "waiting": "该你了",
    "running": "干着呢",
    "idle": "摸鱼中",
    "stale": "没声儿了",
}

SESSION_START = "会话起来了"
THINKING = "正在琢磨"
DONE = "干完了"
WAITING_DEFAULT = "等你点头"
IDLE_DEFAULT = "等你下一句"

EMPTY_LIST = "一个会话都没有。随便找个项目敲 claude 就出来了。"
CONN_ON = "连上了"
CONN_OFF = "断了，正重连"
ARM_BUTTON = "叫醒我"
ARMED_BUTTON = "盯着呢"
UNPAIRED = "还没配对。在宿主机托盘里点『配对新设备』，然后从那儿复制地址过来。"
PAIRED_BALLOON = "刚配对了一台设备"
```

- [ ] **Step 4: 跑测试确认通过**

Run: `python -m pytest tests/test_phrases.py -v`
Expected: 4 passed

- [ ] **Step 5: Commit**

```bash
git add phrases.py tests/test_phrases.py
git commit -m "feat: 三端统一口语文案常量"
```

---

### Task 3: `sessions.py` —— 会话状态机

**Files:**
- Create: `sessions.py`
- Test: `tests/test_sessions.py`
- Modify: `server.py`（本任务只是把逻辑搬出来，`server.py` 的改造留到 Task 7）

**Interfaces:**
- Consumes: `phrases`
- Produces:
  - `HOST: str`、`IDLE_TIMEOUT: int`、`SESSIONS: dict[str, dict]`
  - `key(session_id: str) -> str`
  - `tail_summary(transcript_path: str | None) -> str`
  - `touch(session_id: str, payload: dict, now: float | None = None) -> dict`
  - `apply_event(event: str, payload: dict, now: float | None = None) -> dict | None`
  - `detail(s: dict, full: bool = True) -> str`
  - `snapshot(metrics: dict, now: float | None = None) -> dict`

关键设计：`tool` 与 `arg` **分开存**，不存拼好的字符串 —— 脱敏就是选择性拼接，不是事后正则。
所有函数接受可注入的 `now`，让超时与排序可测。

- [ ] **Step 1: 写失败的测试**

`tests/test_sessions.py`：

```python
import json

import phrases
import sessions


def ev(event, sid="s1", now=1000.0, **payload):
    payload.setdefault("session_id", sid)
    return sessions.apply_event(event, payload, now=now)


def test_key_includes_hostname():
    assert sessions.key("abc") == f"{sessions.HOST}:abc"


def test_session_start_is_idle():
    s = ev("session-start", cwd="D:/proj/Amo")
    assert s["state"] == "idle"
    assert s["phrase"] == phrases.SESSION_START
    assert s["host"] == sessions.HOST


def test_name_falls_back_to_cwd_dirname():
    s = ev("session-start", cwd="D:/proj/Amo")
    assert s["name"] == "Amo"


def test_session_title_wins_over_cwd():
    s = ev("session-start", cwd="D:/proj/Amo", session_title="恐龙公园初始化")
    assert s["name"] == "恐龙公园初始化"


def test_prompt_is_running():
    s = ev("prompt")
    assert s["state"] == "running"
    assert s["phrase"] == phrases.THINKING


def test_tool_stores_name_and_arg_separately():
    s = ev("tool", tool_name="Bash", tool_input={"command": "git push origin main"})
    assert s["state"] == "running"
    assert s["tool"] == "Bash"
    assert s["arg"] == "git push origin main"


def test_tool_arg_falls_back_through_fields():
    s = ev("tool", tool_name="Read", tool_input={"file_path": "D:/a/b.py"})
    assert s["arg"] == "D:/a/b.py"


def test_permission_notify_is_waiting():
    s = ev("notify", notification_type="permission_prompt", message="要跑这条命令吗")
    assert s["state"] == "waiting"
    assert s["phrase"] == "要跑这条命令吗"


def test_idle_notify_is_idle_not_waiting():
    """permission 与 idle 必须分开，混在一起会让告警变成狼来了。"""
    s = ev("notify", notification_type="idle_prompt", message="等着呢")
    assert s["state"] == "idle"


def test_notify_without_message_uses_default():
    s = ev("notify", notification_type="permission_prompt")
    assert s["phrase"] == phrases.WAITING_DEFAULT


def test_session_end_removes_session():
    ev("session-start")
    assert sessions.SESSIONS
    ev("session-end")
    assert not sessions.SESSIONS


def test_event_without_session_id_is_ignored():
    assert sessions.apply_event("prompt", {}) is None
    assert not sessions.SESSIONS


def test_tool_fields_cleared_on_non_tool_event():
    ev("tool", tool_name="Bash", tool_input={"command": "ls"})
    s = ev("prompt")
    assert s["tool"] == ""


def test_detail_full_vs_redacted():
    s = ev("tool", tool_name="Bash", tool_input={"command": "git push origin main"})
    assert sessions.detail(s, full=True) == "Bash: git push origin main"
    assert sessions.detail(s, full=False) == "Bash"


def test_detail_uses_phrase_when_no_tool():
    s = ev("prompt")
    assert sessions.detail(s, full=False) == phrases.THINKING


def test_stop_uses_transcript_summary(tmp_path):
    t = tmp_path / "t.jsonl"
    t.write_text(json.dumps({
        "type": "assistant",
        "message": {"content": [{"type": "text", "text": "改完了，测试全绿"}]},
    }) + "\n", encoding="utf-8")

    ev("session-start", transcript_path=str(t))
    s = ev("stop", transcript_path=str(t))
    assert s["phrase"] == "改完了，测试全绿"


def test_stop_without_transcript_falls_back():
    s = ev("stop")
    assert s["phrase"] == phrases.DONE


def test_tail_summary_missing_file_is_empty():
    assert sessions.tail_summary("D:/nope/nothing.jsonl") == ""
    assert sessions.tail_summary(None) == ""


def test_running_goes_stale_after_timeout():
    ev("prompt", now=1000.0)
    snap = sessions.snapshot({}, now=1000.0 + sessions.IDLE_TIMEOUT + 1)
    assert snap["sessions"][0]["state"] == "stale"


def test_idle_does_not_go_stale():
    """只有 running 会失联；空闲本来就没事件。"""
    ev("session-start", now=1000.0)
    snap = sessions.snapshot({}, now=1000.0 + sessions.IDLE_TIMEOUT + 1)
    assert snap["sessions"][0]["state"] == "idle"


def test_snapshot_sorts_waiting_first():
    ev("session-start", sid="a", cwd="D:/a")
    ev("prompt", sid="b", cwd="D:/b")
    ev("notify", sid="c", cwd="D:/c", notification_type="permission_prompt")

    states = [s["state"] for s in sessions.snapshot({}, now=1000.0)["sessions"]]
    assert states == ["waiting", "running", "idle"]


def test_snapshot_carries_host_and_full_detail():
    ev("tool", tool_name="Bash", tool_input={"command": "ls -la"})
    snap = sessions.snapshot({"cpu": 12}, now=1000.0)

    assert snap["host"] == sessions.HOST
    assert snap["metrics"] == {"cpu": 12}
    assert snap["sessions"][0]["detail"] == "Bash: ls -la"
```

- [ ] **Step 2: 跑测试确认失败**

Run: `python -m pytest tests/test_sessions.py -v`
Expected: FAIL —— `ModuleNotFoundError: No module named 'sessions'`

- [ ] **Step 3: 写实现**

`sessions.py`：

```python
"""会话状态机。

数据来自 Claude Code 的 http hook。刻意只用事后事件（PostToolUse / Stop /
Notification），不用 PreToolUse —— 后者可阻断，仪表盘卡住会拖慢被监控对象。
代价是工具执行中的几秒显示的是上一个工具。
"""
import json
import os
import socket
import time
from pathlib import Path

import phrases

HOST = socket.gethostname()
IDLE_TIMEOUT = 90
SESSIONS: dict[str, dict] = {}

_ORDER = {"waiting": 0, "running": 1, "idle": 2, "stale": 3}


def key(session_id: str) -> str:
    return f"{HOST}:{session_id}"


def tail_summary(transcript_path: str | None) -> str:
    """从 transcript jsonl 尾部抓最后一段 assistant 文本当摘要。"""
    if not transcript_path or not os.path.exists(transcript_path):
        return ""
    try:
        with open(transcript_path, "rb") as f:
            f.seek(0, 2)
            f.seek(max(0, f.tell() - 65536))
            lines = f.read().decode("utf-8", "ignore").splitlines()
        for line in reversed(lines):
            try:
                obj = json.loads(line)
            except Exception:
                continue
            if obj.get("type") != "assistant":
                continue
            for block in obj.get("message", {}).get("content", []):
                if block.get("type") == "text" and block.get("text", "").strip():
                    return block["text"].strip().replace("\n", " ")[:180]
    except Exception:
        pass
    return ""


def touch(session_id: str, payload: dict, now: float | None = None) -> dict:
    now = time.time() if now is None else now
    k = key(session_id)
    s = SESSIONS.setdefault(k, {"id": session_id, "host": HOST, "started": now,
                                "tool": "", "arg": "", "phrase": ""})
    cwd = payload.get("cwd") or s.get("cwd") or ""
    s["cwd"] = cwd
    s["name"] = (payload.get("session_title")
                 or s.get("name")
                 or (Path(cwd).name if cwd else session_id[:8]))
    s["transcript"] = payload.get("transcript_path") or s.get("transcript")
    s["last_event"] = now
    return s


def apply_event(event: str, payload: dict, now: float | None = None) -> dict | None:
    sid = payload.get("session_id")
    if not sid:
        return None

    if event == "session-end":
        SESSIONS.pop(key(sid), None)
        return None

    s = touch(sid, payload, now)
    s["tool"] = ""
    s["arg"] = ""

    if event == "session-start":
        s.update(state="idle", phrase=phrases.SESSION_START)
    elif event == "prompt":
        s.update(state="running", phrase=phrases.THINKING)
    elif event == "tool":
        ti = payload.get("tool_input") or {}
        s.update(state="running", phrase="",
                 tool=payload.get("tool_name", ""),
                 arg=str(ti.get("command") or ti.get("file_path")
                         or ti.get("pattern") or ""))
    elif event == "notify":
        kind = (payload.get("notification_type") or payload.get("matcher") or "").lower()
        msg = payload.get("message", "")
        if "permission" in kind or "permission" in msg.lower():
            s.update(state="waiting", phrase=msg or phrases.WAITING_DEFAULT)
        else:
            s.update(state="idle", phrase=msg or phrases.IDLE_DEFAULT)
    elif event == "stop":
        s.update(state="idle",
                 phrase=tail_summary(s.get("transcript")) or phrases.DONE)

    return s


def detail(s: dict, full: bool = True) -> str:
    """detail 是"当前在干嘛"，不是摘要。full=False 时脱敏到工具名。"""
    tool = s.get("tool") or ""
    if not tool:
        return s.get("phrase", "")
    arg = s.get("arg") or ""
    return f"{tool}: {arg}" if (full and arg) else tool


def snapshot(metrics: dict, now: float | None = None) -> dict:
    now = time.time() if now is None else now
    out = []
    for s in list(SESSIONS.values()):
        last = s.get("last_event", now)
        stale = now - last > IDLE_TIMEOUT
        state = "stale" if (stale and s.get("state") == "running") else s.get("state", "idle")
        out.append({**s, "state": state,
                    "detail": detail(s, full=True),
                    "age": round(now - last)})
    out.sort(key=lambda x: (_ORDER.get(x["state"], 4), x["name"]))
    return {"metrics": metrics, "sessions": out, "ts": now, "host": HOST}
```

- [ ] **Step 4: 跑测试确认通过**

Run: `python -m pytest tests/test_sessions.py -v`
Expected: 22 passed

- [ ] **Step 5: Commit**

```bash
git add sessions.py tests/test_sessions.py
git commit -m "feat: 会话状态机独立成模块，tool 与 arg 分开存以支持脱敏"
```

---

### Task 4: `metrics.py` —— 指标采集

**Files:**
- Create: `metrics.py`
- Test: `tests/test_metrics.py`

**Interfaces:**
- Consumes: 无
- Produces:
  - `HISTORY: int`
  - `current() -> dict` —— 返回含 `cpu/mem/disk/gpu/vram/net_up/net_down/hist` 的快照
  - `start_collector() -> None` —— 启动守护线程，幂等
  - `_sample(prev_net, prev_t, now, counters) -> dict` —— 纯函数，供测试

关键设计：`psutil.cpu_percent(interval=1.0)` 自带 1 秒阻塞，**它就是循环节拍**，
不要换成非阻塞版本再加 `sleep`。速率计算抽成纯函数 `_sample` 以便测试。

- [ ] **Step 1: 写失败的测试**

`tests/test_metrics.py`：

```python
import metrics


class FakeCounters:
    def __init__(self, sent, recv):
        self.bytes_sent = sent
        self.bytes_recv = recv


def test_sample_computes_rate_per_second():
    prev = FakeCounters(1000, 2000)
    cur = FakeCounters(1000 + 4096, 2000 + 8192)
    out = metrics._sample(prev, prev_t=100.0, now=102.0, counters=cur)

    assert out["net_up"] == 2048       # 4096 字节 / 2 秒
    assert out["net_down"] == 4096


def test_sample_survives_zero_elapsed():
    """时钟没走时不能除零。"""
    prev = FakeCounters(0, 0)
    cur = FakeCounters(10, 10)
    out = metrics._sample(prev, prev_t=100.0, now=100.0, counters=cur)

    assert out["net_up"] >= 0
    assert out["net_down"] >= 0


def test_current_has_all_keys():
    m = metrics.current()
    for k in ("cpu", "mem", "disk", "gpu", "vram", "net_up", "net_down", "hist"):
        assert k in m


def test_history_buckets_exist():
    m = metrics.current()
    for k in ("cpu", "mem", "gpu", "net"):
        assert k in m["hist"]


def test_gpu_is_none_without_nvidia():
    """没有 N 卡时静默降级为 None，客户端渲染成 —。"""
    m = metrics.current()
    assert m["gpu"] is None or isinstance(m["gpu"], (int, float))
```

- [ ] **Step 2: 跑测试确认失败**

Run: `python -m pytest tests/test_metrics.py -v`
Expected: FAIL —— `ModuleNotFoundError: No module named 'metrics'`

- [ ] **Step 3: 写实现**

`metrics.py`：

```python
"""系统指标采集。

采集在独立守护线程里跑；psutil.cpu_percent(interval=1.0) 自带 1 秒阻塞，
那句话就是循环节拍，别换成非阻塞版本再加 sleep。
"""
import os
import threading
import time
from collections import deque

import psutil

HISTORY = 60          # 保留 60 个采样点画 sparkline

_metrics = {"cpu": 0, "mem": 0, "disk": 0, "gpu": None, "vram": None,
            "net_up": 0, "net_down": 0,
            "hist": {k: [] for k in ("cpu", "mem", "gpu", "net")}}

_started = False
_lock = threading.Lock()

try:
    import pynvml
    pynvml.nvmlInit()
    _gpu = pynvml.nvmlDeviceGetHandleByIndex(0)
except Exception:
    _gpu = None


def current() -> dict:
    return dict(_metrics)


def _sample(prev_net, prev_t: float, now: float, counters) -> dict:
    dt = max(now - prev_t, 0.001)
    return {
        "net_up": round((counters.bytes_sent - prev_net.bytes_sent) / dt),
        "net_down": round((counters.bytes_recv - prev_net.bytes_recv) / dt),
    }


def _read_gpu():
    if not _gpu:
        return None, None
    try:
        util = pynvml.nvmlDeviceGetUtilizationRates(_gpu).gpu
        mem = pynvml.nvmlDeviceGetMemoryInfo(_gpu)
        return util, round(mem.used / mem.total * 100)
    except Exception:
        return None, None


def _collect_loop():
    prev_net = psutil.net_io_counters()
    prev_t = time.time()
    hist = {k: deque(maxlen=HISTORY) for k in ("cpu", "mem", "gpu", "net")}

    while True:
        cpu = psutil.cpu_percent(interval=1.0)      # 这一句就是节拍
        now = time.time()
        counters = psutil.net_io_counters()
        rates = _sample(prev_net, prev_t, now, counters)
        prev_net, prev_t = counters, now

        mem = psutil.virtual_memory().percent
        disk = psutil.disk_usage(os.environ.get("SystemDrive", "C:") + "\\").percent
        gpu, vram = _read_gpu()

        hist["cpu"].append(cpu)
        hist["mem"].append(mem)
        hist["gpu"].append(gpu or 0)
        hist["net"].append(round((rates["net_up"] + rates["net_down"]) / 1024))

        _metrics.update(cpu=cpu, mem=mem, disk=disk, gpu=gpu, vram=vram,
                        hist={k: list(v) for k, v in hist.items()}, **rates)


def start_collector() -> None:
    global _started
    with _lock:
        if _started:
            return
        _started = True
    threading.Thread(target=_collect_loop, daemon=True).start()
```

- [ ] **Step 4: 跑测试确认通过**

Run: `python -m pytest tests/test_metrics.py -v`
Expected: 5 passed

- [ ] **Step 5: Commit**

```bash
git add metrics.py tests/test_metrics.py
git commit -m "feat: 指标采集独立成模块，速率计算抽成可测纯函数"
```

---

### Task 5: `security.py` —— 令牌、来源、配对窗口

**Files:**
- Create: `security.py`
- Test: `tests/test_security.py`

**Interfaces:**
- Consumes: 无
- Produces:
  - `LOOPBACK: set[str]`
  - `check_token(provided: str | None, expected: str) -> bool`
  - `is_loopback(host: str | None) -> bool`
  - `class PairingWindow` —— `__init__(on_pair: Callable[[str], None] | None = None)`、
    `open(seconds: int = 60, now: float | None = None) -> None`、
    `is_open(now: float | None = None) -> bool`、`close() -> None`、
    `record(peer: str) -> None`（触发 `on_pair` 回调）

- [ ] **Step 1: 写失败的测试**

`tests/test_security.py`：

```python
import security


def test_correct_token_passes():
    assert security.check_token("abc123", "abc123") is True


def test_wrong_token_fails():
    assert security.check_token("nope", "abc123") is False


def test_missing_token_fails():
    assert security.check_token(None, "abc123") is False
    assert security.check_token("", "abc123") is False


def test_length_mismatch_does_not_raise():
    """compare_digest 对不等长输入会抛，必须先挡住。"""
    assert security.check_token("short", "muchlongertoken") is False


def test_loopback_detection():
    assert security.is_loopback("127.0.0.1") is True
    assert security.is_loopback("::1") is True
    assert security.is_loopback("192.168.1.50") is False
    assert security.is_loopback(None) is False


def test_pairing_window_closed_by_default():
    w = security.PairingWindow()
    assert w.is_open(now=1000.0) is False


def test_pairing_window_opens_for_60s():
    w = security.PairingWindow()
    w.open(seconds=60, now=1000.0)

    assert w.is_open(now=1000.0) is True
    assert w.is_open(now=1059.0) is True
    assert w.is_open(now=1061.0) is False


def test_pairing_window_can_be_closed_early():
    w = security.PairingWindow()
    w.open(seconds=60, now=1000.0)
    w.close()
    assert w.is_open(now=1001.0) is False


def test_record_fires_callback_with_peer():
    seen = []
    w = security.PairingWindow(on_pair=seen.append)
    w.record("192.168.1.77")
    assert seen == ["192.168.1.77"]


def test_record_without_callback_does_not_raise():
    security.PairingWindow().record("192.168.1.77")
```

- [ ] **Step 2: 跑测试确认失败**

Run: `python -m pytest tests/test_security.py -v`
Expected: FAIL —— `ModuleNotFoundError: No module named 'security'`

- [ ] **Step 3: 写实现**

`security.py`：

```python
"""令牌校验、来源判定、配对窗口。

信任边界就是令牌：设备只能读取自己配对过的主机。同事的机器在 mDNS 里
发现得到，但拿不到令牌，因此不会出现在你的设备上。
"""
import secrets
import time
from typing import Callable

LOOPBACK = {"127.0.0.1", "::1", "localhost"}


def check_token(provided: str | None, expected: str) -> bool:
    if not provided or len(provided) != len(expected):
        return False
    return secrets.compare_digest(provided, expected)


def is_loopback(host: str | None) -> bool:
    return host in LOOPBACK if host else False


class PairingWindow:
    """托盘点"配对新设备"后开启的一段有限时间窗口。

    窗口内允许多台设备配对（平板 + ESP32 一次搞定），但每次成功都回调
    一次通知 —— 若有人趁窗口偷配，用户能立刻看见。
    """

    def __init__(self, on_pair: Callable[[str], None] | None = None):
        self.until = 0.0
        self.on_pair = on_pair

    def open(self, seconds: int = 60, now: float | None = None) -> None:
        self.until = (time.time() if now is None else now) + seconds

    def is_open(self, now: float | None = None) -> bool:
        return (time.time() if now is None else now) < self.until

    def close(self) -> None:
        self.until = 0.0

    def record(self, peer: str) -> None:
        if self.on_pair:
            self.on_pair(peer)
```

- [ ] **Step 4: 跑测试确认通过**

Run: `python -m pytest tests/test_security.py -v`
Expected: 10 passed

- [ ] **Step 5: Commit**

```bash
git add security.py tests/test_security.py
git commit -m "feat: 令牌校验、回环判定与配对窗口"
```

---

### Task 6: `tiny.py` —— MCU 三行格式

**Files:**
- Create: `tiny.py`
- Test: `tests/test_tiny.py`

**Interfaces:**
- Consumes: `sessions.detail`
- Produces: `TINY_VERSION: int`、`render_tiny(snap: dict, full: bool = False) -> str`

格式（严格三行，ESP32 侧按行切分）：

```
1|3,1,2|WORKSTATION
恐龙公园初始化\tBash
12,41,3,8
```

- [ ] **Step 1: 写失败的测试**

`tests/test_tiny.py`：

```python
import sessions
import tiny


def make_snap(sess, metrics=None):
    return {"sessions": sess, "host": "WORKSTATION",
            "metrics": metrics or {"cpu": 12.4, "mem": 41.2, "gpu": 3,
                                   "net_up": 4096, "net_down": 4192}}


def sess(state, name, tool="", arg="", phrase=""):
    return {"state": state, "name": name, "tool": tool, "arg": arg, "phrase": phrase}


def test_three_lines_exactly():
    out = tiny.render_tiny(make_snap([]))
    assert len(out.split("\n")) == 3


def test_counts_and_host_on_first_line():
    s = [sess("waiting", "a"), sess("running", "b"), sess("running", "c"),
         sess("idle", "d")]
    line1 = tiny.render_tiny(make_snap(s)).split("\n")[0]
    assert line1 == "1|1,2,1|WORKSTATION"


def test_top_session_is_the_first_one():
    """snapshot() 已按 waiting → running → idle 排过序，取第一条即可。"""
    s = [sess("waiting", "急事", tool="Bash", arg="rm -rf /tmp/x"),
         sess("running", "别的")]
    line2 = tiny.render_tiny(make_snap(s)).split("\n")[1]
    assert line2.startswith("急事\t")


def test_detail_is_redacted_by_default():
    s = [sess("waiting", "恐龙公园初始化", tool="Bash", arg="git push origin main")]
    line2 = tiny.render_tiny(make_snap(s)).split("\n")[1]
    assert line2 == "恐龙公园初始化\tBash"


def test_detail_full_when_requested():
    s = [sess("waiting", "恐龙公园初始化", tool="Bash", arg="git push origin main")]
    line2 = tiny.render_tiny(make_snap(s), full=True).split("\n")[1]
    assert line2 == "恐龙公园初始化\tBash: git push origin main"


def test_empty_second_line_when_no_sessions():
    assert tiny.render_tiny(make_snap([])).split("\n")[1] == ""


def test_idle_only_still_shows_that_session():
    s = [sess("idle", "闲着的", phrase="摸鱼中")]
    line2 = tiny.render_tiny(make_snap(s)).split("\n")[1]
    assert line2.startswith("闲着的\t")


def test_metrics_line_rounds():
    line3 = tiny.render_tiny(make_snap([])).split("\n")[2]
    assert line3 == "12,41,3,8"        # (4096+4192)/1024 = 8.09 -> 8


def test_gpu_absent_is_minus_one():
    snap = make_snap([], metrics={"cpu": 5, "mem": 5, "gpu": None,
                                  "net_up": 0, "net_down": 0})
    assert tiny.render_tiny(snap).split("\n")[2] == "5,5,-1,0"


def test_no_cwd_leaks_into_output():
    """桌上小屏别人走过就能看见，绝不下发路径。"""
    s = [dict(sess("running", "x", tool="Read", arg="D:/secret/plan.md"),
              cwd="D:/secret")]
    out = tiny.render_tiny(make_snap(s))
    assert "D:/secret" not in out
```

- [ ] **Step 2: 跑测试确认失败**

Run: `python -m pytest tests/test_tiny.py -v`
Expected: FAIL —— `ModuleNotFoundError: No module named 'tiny'`

- [ ] **Step 3: 写实现**

`tiny.py`：

```python
"""给 MCU 的极简端点渲染。

纯文本三行，ESP32 侧按行切分即可，不需要 cJSON。第一行带版本号，
以后加字段不至于把已烧录的固件打死。
"""
import sessions

TINY_VERSION = 1


def render_tiny(snap: dict, full: bool = False) -> str:
    ss = snap["sessions"]

    def n(state: str) -> int:
        return sum(1 for s in ss if s["state"] == state)

    line1 = f"{TINY_VERSION}|{n('waiting')},{n('running')},{n('idle')}|{snap['host']}"

    top = ss[0] if ss else None
    line2 = f"{top['name']}\t{sessions.detail(top, full=full)}" if top else ""

    m = snap["metrics"]
    gpu = m.get("gpu")
    net_kb = round((m.get("net_up", 0) + m.get("net_down", 0)) / 1024)
    line3 = (f"{round(m.get('cpu', 0))},{round(m.get('mem', 0))},"
             f"{-1 if gpu is None else round(gpu)},{net_kb}")

    return "\n".join([line1, line2, line3])
```

- [ ] **Step 4: 跑测试确认通过**

Run: `python -m pytest tests/test_tiny.py -v`
Expected: 10 passed

- [ ] **Step 5: Commit**

```bash
git add tiny.py tests/test_tiny.py
git commit -m "feat: /api/tiny 的三行文本渲染，默认脱敏且不下发路径"
```

---

### Task 7: `server.py` 重装配 + `static/` 落地

**Files:**
- Modify: `server.py`（整体重写为薄装配层）
- Create: `static/`（目录）
- Move: `index.html` → `static/index.html`
- Test: `tests/test_api.py`

**Interfaces:**
- Consumes: `config`、`metrics`、`sessions`、`security`、`tiny`
- Produces:
  - `PORT: int = 8787`、`CFG: config.Config`、`PAIRING: security.PairingWindow`
  - `app: FastAPI`
  - `resource_path(rel: str) -> Path`
  - `STATIC: Path`

这是让服务器**第一次真正能启动**的任务：`static/` 目录此前根本不存在。

- [ ] **Step 1: 把页面移进 `static/`**

```bash
mkdir static
git mv index.html static/index.html
```

- [ ] **Step 2: 写失败的测试**

`tests/test_api.py`：

```python
import pytest
from fastapi.testclient import TestClient

import server


@pytest.fixture
def client():
    return TestClient(server.app)


@pytest.fixture
def tok():
    return server.CFG.token


def hook(client, event, **payload):
    payload.setdefault("session_id", "s1")
    return client.post(f"/hook/{event}", json=payload)


# ---- 静态资源 ----

def test_static_dir_exists():
    assert server.STATIC.is_dir()
    assert (server.STATIC / "index.html").is_file()


def test_index_served_without_token(client):
    assert client.get("/").status_code == 200


# ---- 鉴权 ----

def test_stream_without_token_is_401(client):
    assert client.get("/api/stream").status_code == 401


def test_stream_with_wrong_token_is_401(client):
    assert client.get("/api/stream?k=deadbeef").status_code == 401


def test_tiny_without_token_is_401(client):
    assert client.get("/api/tiny").status_code == 401


def test_tiny_with_token_is_200(client, tok):
    assert client.get(f"/api/tiny?k={tok}").status_code == 200


def test_token_accepted_via_header(client, tok):
    r = client.get("/api/tiny", headers={"X-Agent-Key": tok})
    assert r.status_code == 200


# ---- hook 来源限制 ----

def test_hook_from_loopback_is_accepted(client):
    assert hook(client, "session-start", cwd="D:/proj/Amo").status_code == 200


def test_hook_rejects_non_loopback_client(client, monkeypatch):
    monkeypatch.setattr(server.security, "is_loopback", lambda h: False)
    assert hook(client, "prompt").status_code == 403


# ---- 端到端 ----

def test_hook_then_tiny_reflects_state(client, tok):
    hook(client, "session-start", cwd="D:/proj/Amo")
    hook(client, "notify", notification_type="permission_prompt", message="批准吗")

    body = client.get(f"/api/tiny?k={tok}").text
    line1, line2, _ = body.split("\n")

    assert line1.startswith("1|1,0,0|")
    assert line2.startswith("Amo\t")


def test_tiny_full_flag_gives_arguments(client, tok):
    hook(client, "tool", cwd="D:/proj/Amo",
         tool_name="Bash", tool_input={"command": "git push origin main"})

    redacted = client.get(f"/api/tiny?k={tok}").text.split("\n")[1]
    full = client.get(f"/api/tiny?k={tok}&d=full").text.split("\n")[1]

    assert redacted == "Amo\tBash"
    assert full == "Amo\tBash: git push origin main"


# ---- 配对 ----

def test_pair_closed_by_default_is_403(client):
    server.PAIRING.close()
    assert client.post("/api/pair").status_code == 403


def test_pair_inside_window_returns_token(client, tok):
    server.PAIRING.open(seconds=60)
    try:
        r = client.post("/api/pair")
        assert r.status_code == 200
        assert r.json()["token"] == tok
        assert r.json()["host_id"] == server.CFG.host_id
    finally:
        server.PAIRING.close()


def test_pair_after_close_is_403_again(client):
    server.PAIRING.open(seconds=60)
    server.PAIRING.close()
    assert client.post("/api/pair").status_code == 403
```

- [ ] **Step 3: 跑测试确认失败**

Run: `python -m pytest tests/test_api.py -v`
Expected: FAIL —— `AttributeError: module 'server' has no attribute 'CFG'`

- [ ] **Step 4: 重写 `server.py`**

```python
"""Agent 控制台 —— 宿主机服务端。

启动: python server.py    监听 0.0.0.0:8787
"""
import asyncio
import json
import sys
from pathlib import Path

from fastapi import Depends, FastAPI, HTTPException, Request
from fastapi.responses import FileResponse, PlainTextResponse, StreamingResponse
from fastapi.staticfiles import StaticFiles

import config
import metrics
import security
import sessions
import tiny

PORT = 8787

CFG = config.load_config()
PAIRING = security.PairingWindow()


def resource_path(rel: str) -> Path:
    """PyInstaller 打包后资源解压在 sys._MEIPASS，源码运行时就是脚本目录。"""
    return Path(getattr(sys, "_MEIPASS", Path(__file__).parent)) / rel


STATIC = resource_path("static")

app = FastAPI()


async def require_token(request: Request) -> None:
    provided = request.query_params.get("k") or request.headers.get("X-Agent-Key")
    if not security.check_token(provided, CFG.token):
        raise HTTPException(401, "需要有效令牌")


async def require_loopback(request: Request) -> None:
    host = request.client.host if request.client else None
    if not security.is_loopback(host):
        raise HTTPException(403, "hook 只接受本机来源")


@app.post("/hook/{event}", dependencies=[Depends(require_loopback)])
async def hook(event: str, request: Request):
    """Claude Code 的 http hook 全部打到这里。必须快速返回 200。"""
    try:
        payload = await request.json()
    except Exception:
        payload = {}
    sessions.apply_event(event, payload)
    return {}


@app.get("/api/stream", dependencies=[Depends(require_token)])
async def stream():
    async def gen():
        while True:
            snap = sessions.snapshot(metrics.current())
            yield f"data: {json.dumps(snap, ensure_ascii=False)}\n\n"
            await asyncio.sleep(1)

    return StreamingResponse(gen(), media_type="text/event-stream",
                             headers={"Cache-Control": "no-cache",
                                      "X-Accel-Buffering": "no"})


@app.get("/api/tiny", response_class=PlainTextResponse,
         dependencies=[Depends(require_token)])
async def tiny_endpoint(d: str = ""):
    snap = sessions.snapshot(metrics.current())
    return tiny.render_tiny(snap, full=(d == "full" or CFG.tiny_detail == "full"))


@app.post("/api/pair")
async def pair(request: Request):
    if not PAIRING.is_open():
        raise HTTPException(403, "配对窗口没开")
    PAIRING.record(request.client.host if request.client else "?")
    return {"token": CFG.token, "host_id": CFG.host_id, "host": sessions.HOST}


@app.get("/")
async def index():
    return FileResponse(STATIC / "index.html")


app.mount("/static", StaticFiles(directory=STATIC), name="static")


if __name__ == "__main__":
    import uvicorn

    metrics.start_collector()
    uvicorn.run(app, host="0.0.0.0", port=PORT, log_level="warning")
```

- [ ] **Step 5: 跑全部测试**

Run: `python -m pytest tests/ -v`
Expected: 全绿

- [ ] **Step 6: 手工验证服务真能起来**

Run: `python server.py`
Expected: 无异常退出。另开一个终端：

```bash
curl -i http://127.0.0.1:8787/
```

Expected: `200 OK` 且返回 HTML（这是修复 spec §0 缺陷 1 的验收点 —— 改造前这里会崩在
`StaticFiles(directory=...)` 上）。再验证鉴权：

```bash
curl -i http://127.0.0.1:8787/api/tiny
```

Expected: `401`。用真实令牌（从 `%APPDATA%\AgentDashboard\config.json` 里取）再试一次，
Expected: `200` 且返回三行文本。验证完 Ctrl+C 停掉。

- [ ] **Step 7: Commit**

```bash
git add server.py static/ tests/test_api.py
git commit -m "feat: server 重装配为薄端点层，static 落地，加令牌鉴权与配对端点"
```

---

### Task 8: `discovery.py` —— mDNS 广播

**Files:**
- Create: `discovery.py`
- Modify: `server.py`（`__main__` 里注册与注销）
- Test: `tests/test_discovery.py`

**Interfaces:**
- Consumes: 无
- Produces:
  - `SERVICE: str = "_agentdash._tcp.local."`
  - `local_ip() -> str`
  - `txt_properties(host_id: str, hostname: str) -> dict[bytes | str, str]`
  - `register(port: int, host_id: str, hostname: str, ip: str | None = None) -> tuple`
  - `unregister(handle: tuple) -> None`

- [ ] **Step 1: 写失败的测试**

`tests/test_discovery.py`：

```python
import discovery


def test_service_type_matches_spec():
    assert discovery.SERVICE == "_agentdash._tcp.local."


def test_txt_contains_only_allowed_keys():
    """TXT 绝不能带任何会话内容 —— 未配对方只该知道"这儿有台控制台"。"""
    props = discovery.txt_properties("a1b2c3d4", "WORKSTATION")
    assert set(props) == {"v", "host", "id"}


def test_txt_values():
    props = discovery.txt_properties("a1b2c3d4", "WORKSTATION")
    assert props["v"] == "1"
    assert props["host"] == "WORKSTATION"
    assert props["id"] == "a1b2c3d4"


def test_txt_carries_no_token():
    props = discovery.txt_properties("a1b2c3d4", "WORKSTATION")
    joined = "".join(str(v) for v in props.values())
    assert "token" not in joined.lower()


def test_local_ip_is_v4_dotted():
    ip = discovery.local_ip()
    parts = ip.split(".")
    assert len(parts) == 4
    assert all(p.isdigit() for p in parts)
```

- [ ] **Step 2: 跑测试确认失败**

Run: `python -m pytest tests/test_discovery.py -v`
Expected: FAIL —— `ModuleNotFoundError: No module named 'discovery'`

- [ ] **Step 3: 写实现**

`discovery.py`：

```python
"""mDNS 广播。

只广播"这台机器上跑着一个 Agent 控制台"，不广播任何会话内容。
设备发现之后仍需令牌才能读数据。
"""
import socket

from zeroconf import ServiceInfo, Zeroconf

SERVICE = "_agentdash._tcp.local."


def local_ip() -> str:
    """取本机在局域网上的出口 IP。

    连一个外部地址但不真的发包，内核会挑出正确的出口网卡 —— 这样能避开
    Hyper-V / WSL 那些虚拟网卡。
    """
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("223.5.5.5", 80))
        return s.getsockname()[0]
    finally:
        s.close()


def txt_properties(host_id: str, hostname: str) -> dict:
    return {"v": "1", "host": hostname, "id": host_id}


def register(port: int, host_id: str, hostname: str, ip: str | None = None):
    ip = ip or local_ip()
    info = ServiceInfo(
        SERVICE,
        f"{hostname}.{SERVICE}",
        addresses=[socket.inet_aton(ip)],
        port=port,
        properties=txt_properties(host_id, hostname),
        server=f"{hostname}.local.",
    )
    zc = Zeroconf(interfaces=[ip])          # 显式绑定局域网口，别广播到虚拟网卡
    zc.register_service(info)
    return (zc, info)


def unregister(handle) -> None:
    zc, info = handle
    try:
        zc.unregister_service(info)
    finally:
        zc.close()
```

- [ ] **Step 4: 跑测试确认通过**

Run: `python -m pytest tests/test_discovery.py -v`
Expected: 5 passed

- [ ] **Step 5: 接进 `server.py` 的 `__main__`**

把 `server.py` 末尾的 `__main__` 块替换为：

```python
if __name__ == "__main__":
    import uvicorn

    import discovery

    metrics.start_collector()
    handle = None
    try:
        handle = discovery.register(PORT, CFG.host_id, sessions.HOST)
    except Exception as e:                  # 没网 / 端口占用都不该拦住主服务
        print(f"mDNS 广播启动失败，设备得手填地址: {e}")
    try:
        uvicorn.run(app, host="0.0.0.0", port=PORT, log_level="warning")
    finally:
        if handle:
            discovery.unregister(handle)
```

- [ ] **Step 6: 手工验证广播**

Run: `python server.py`，另开终端：

```bash
python -c "from zeroconf import Zeroconf, ServiceBrowser; import time; zc=Zeroconf(); ServiceBrowser(zc,'_agentdash._tcp.local.',handlers=[lambda **kw: print(kw)]); time.sleep(3); zc.close()"
```

Expected: 打印出发现事件。验证完 Ctrl+C 停掉。

- [ ] **Step 7: Commit**

```bash
git add discovery.py tests/test_discovery.py server.py
git commit -m "feat: mDNS 广播服务，TXT 只含主机名与 host_id"
```

---

### Task 9: `static/index.html` —— 令牌、文案、机器名

**Files:**
- Modify: `static/index.html`
- Test: 手工验证（页面无自动化测试框架，本轮不引入）

**Interfaces:**
- Consumes: `GET /api/stream?k=<token>` 返回的 `{metrics, sessions, ts, host}`
- Produces: 无（终端）

三处改动，视觉语言（顶部 6px 注意力条、左侧色条、四色语义）一律不动。

- [ ] **Step 1: 令牌处理**

在 `<script>` 开头、`connect()` 定义之前插入：

```javascript
/* ── 令牌：URL 带一次，之后存起来 ── */
const KEY_NAME = 'agentdash_token';
const urlKey = new URLSearchParams(location.search).get('k');
if (urlKey) {
  localStorage.setItem(KEY_NAME, urlKey);
  history.replaceState(null, '', location.pathname);   // 别把令牌留在地址栏
}
const TOKEN = localStorage.getItem(KEY_NAME);
```

把 `connect()` 的第一行改为：

```javascript
function connect() {
  if (!TOKEN) {
    $('#conn').textContent = '未配对';
    $('#empty').hidden = false;
    $('#empty').textContent = '还没配对。在宿主机托盘里点『配对新设备』，然后从那儿复制地址过来。';
    return;
  }
  const es = new EventSource('/api/stream?k=' + encodeURIComponent(TOKEN));
```

- [ ] **Step 2: 文案换成口语版**

把 `LABEL` 常量替换为：

```javascript
const LABEL = {
  running: '干着呢，别催',
  waiting: '哥们儿，该你了',
  idle:    '摸鱼中，等你发话',
  stale:   '没声儿了，人呢？',
};
```

其余文案逐处替换：

| 位置 | 原文 | 新文案 |
|---|---|---|
| `#empty` 默认内容 | 暂无会话。在任意项目里启动 Claude Code 就会出现在这里。 | 一个会话都没有。随便找个项目敲 claude 就出来了。 |
| `es.onopen` | 已连接 | 连上了 |
| `es.onerror` | 断开，重连中 | 断了，正重连 |
| `#arm` 按钮初始 | 开启提醒 | 叫醒我 |
| `#arm` 点击后 | 提醒已开启 | 盯着呢 |
| `startAlert` 通知标题 | 需要你处理 | 哥们儿，该你了 |
| 标题闪烁文本 | ⚠ 需要你处理 | ⚠ 该你了 |

- [ ] **Step 3: 会话卡片显示机器名**

把 `renderSessions` 里那一行 `<div class="path">` 替换为：

```javascript
        <div class="path">${esc(s.host || '')} · ${esc(s.cwd || '')}</div>
```

- [ ] **Step 4: 手工验证**

Run: `python server.py`，浏览器打开 `http://localhost:8787/?k=<令牌>`

逐条确认：

1. 页面加载后地址栏里的 `?k=` 已被清掉，刷新仍能连上（令牌进了 localStorage）
2. 右上角显示「连上了」
3. 无会话时显示新的空列表文案
4. 用另一个隐私窗口打开不带 `?k=` 的地址 → 显示「未配对」提示，且不发起 SSE
5. 在任意项目跑一次 Claude Code，卡片出现且路径行形如 `WORKSTATION · D:\...`
6. 触发一次权限询问 → 顶部 6px 条变琥珀并呼吸，卡片显示「哥们儿，该你了」

- [ ] **Step 5: Commit**

```bash
git add static/index.html
git commit -m "feat: 页面端令牌处理、口语文案与机器名字段"
```

---

### Task 10: `tray.py` —— 配对菜单与带令牌地址

**Files:**
- Modify: `tray.py`
- Test: `tests/test_tray.py`（只测纯函数，GUI 部分手工验证）

**Interfaces:**
- Consumes: `server.CFG`、`server.PAIRING`、`sessions.snapshot`、`metrics.current`、`phrases`
- Produces: `tablet_url(ip: str, port: int, token: str) -> str`、`overall_state(sessions: list) -> str`

- [ ] **Step 1: 写失败的测试**

`tests/test_tray.py`：

```python
import tray


def test_tablet_url_carries_token():
    url = tray.tablet_url("192.168.1.100", 8787, "7f3a9b2c")
    assert url == "http://192.168.1.100:8787/?k=7f3a9b2c"


def test_overall_state_waiting_wins():
    ss = [{"state": "idle"}, {"state": "running"}, {"state": "waiting"}]
    assert tray.overall_state(ss) == "waiting"


def test_overall_state_stale_beats_running():
    ss = [{"state": "running"}, {"state": "stale"}]
    assert tray.overall_state(ss) == "stale"


def test_overall_state_running_beats_idle():
    assert tray.overall_state([{"state": "idle"}, {"state": "running"}]) == "running"


def test_overall_state_empty_is_off():
    assert tray.overall_state([]) == "off"


def test_colors_match_spec():
    assert tray.COLORS["waiting"] == (255, 176, 32)     # #FFB020
    assert tray.COLORS["running"] == (63, 191, 216)     # #3FBFD8
    assert tray.COLORS["stale"] == (226, 86, 74)        # #E2564A
```

- [ ] **Step 2: 跑测试确认失败**

Run: `python -m pytest tests/test_tray.py -v`
Expected: FAIL —— `AttributeError: module 'tray' has no attribute 'tablet_url'`

- [ ] **Step 3: 改 `tray.py`**

在 `import` 区加上：

```python
import phrases
import metrics
import sessions
```

把 `local_ip()` 之后插入两个纯函数：

```python
def tablet_url(ip: str, port: int, token: str) -> str:
    return f"http://{ip}:{port}/?k={token}"


def overall_state(ss: list) -> str:
    """整盘状态：等待 > 失联 > 运行 > 空闲 > 无。"""
    if any(s["state"] == "waiting" for s in ss):
        return "waiting"
    if any(s["state"] == "stale" for s in ss):
        return "stale"
    if any(s["state"] == "running" for s in ss):
        return "running"
    return "idle" if ss else "off"
```

把 `watch()` 里重算状态那一段替换为：

```python
        waiting = [s for s in ss if s["state"] == "waiting"]
        state = overall_state(ss)

        icon.icon = make_icon(state)
        icon.title = (f"{phrases.STATE_LABEL['waiting']}：{waiting[0]['name']}" if waiting
                      else f"{len(ss)} 个会话 · {phrases.STATE_LABEL_SHORT.get(state, state)}")

        if len(waiting) > last_waiting:
            try:
                icon.notify(waiting[0].get("detail", phrases.WAITING_DEFAULT),
                            phrases.STATE_LABEL["waiting"])
            except Exception:
                pass
        last_waiting = len(waiting)
```

把 `watch()` 取数据那一句改为：

```python
        try:
            ss = sessions.snapshot(metrics.current())["sessions"]
        except Exception:
            ss = []
```

在 `main()` 里，启动 uvicorn 之前加上采集线程与配对回调：

```python
    metrics.start_collector()

    def on_pair(peer: str):
        try:
            icon.notify(f"{peer}", phrases.PAIRED_BALLOON)
        except Exception:
            pass

    server.PAIRING.on_pair = on_pair
```

（注意 `icon` 在 `main()` 里晚于此处定义 —— 把这段挪到 `icon = pystray.Icon(...)` 之后、
`icon.run()` 之前。）

菜单替换为：

```python
    menu=pystray.Menu(
        pystray.MenuItem("打开控制台",
                         lambda: webbrowser.open(f"http://localhost:{PORT}"),
                         default=True),
        pystray.MenuItem(lambda i: f"复制平板地址  {local_ip()}:{PORT}",
                         lambda: copy(tablet_url(local_ip(), PORT, server.CFG.token))),
        pystray.MenuItem("配对新设备（60 秒）",
                         lambda: server.PAIRING.open(seconds=60)),
        pystray.Menu.SEPARATOR,
        pystray.MenuItem("开机自启", toggle_autostart, checked=lambda i: autostart_on()),
        pystray.MenuItem("退出", lambda i: i.stop()),
    ))
```

- [ ] **Step 4: 跑测试确认通过**

Run: `python -m pytest tests/test_tray.py -v`
Expected: 6 passed

- [ ] **Step 5: 手工验证托盘**

Run: `python tray.py`

逐条确认：

1. 托盘出现图标，悬浮显示「N 个会话 · 摸鱼中」
2. 点「复制平板地址」→ 粘贴出来的 URL 带 `?k=`，浏览器打开能直接看到数据
3. 点「配对新设备（60 秒）」→ 立刻在另一个终端跑
   `curl -X POST http://127.0.0.1:8787/api/pair`，Expected: 返回令牌 JSON 且弹出气泡
4. 等 60 秒后再跑同一条 curl，Expected: `403`
5. 触发一次权限询问 → 图标变琥珀实心并弹一次气泡；持续等待期间不重复弹

- [ ] **Step 6: 跑全量测试**

Run: `python -m pytest tests/ -v`
Expected: 全绿

- [ ] **Step 7: Commit**

```bash
git add tray.py tests/test_tray.py
git commit -m "feat: 托盘加配对菜单与带令牌地址，文案统一"
```

---

### Task 11: 打包与 hooks 配置同步

**Files:**
- Modify: `build.bat`、`hooks-snippet.json`、`README.md`
- Test: 手工验证

**Interfaces:**
- Consumes: 全部前序任务
- Produces: 可分发的 `dist/AgentDashboard/AgentDashboard.exe`

- [ ] **Step 1: 改 `build.bat` 为 `--onedir`**

`--onefile` 每次启动要解压到 `%TEMP%`（冷启动 2-3 秒，且杀软有概率误报）。
常驻托盘的程序一天只启动一次，`--onedir` 是更好的取舍。

```bat
@echo off
pip install -r requirements-dev.txt

pyinstaller --noconfirm --clean ^
  --name AgentDashboard ^
  --onedir ^
  --noconsole ^
  --add-data "static;static" ^
  --collect-all uvicorn ^
  --collect-all fastapi ^
  --collect-all starlette ^
  --collect-all zeroconf ^
  --collect-all pynvml ^
  --hidden-import psutil ^
  tray.py

echo.
echo 产物: dist\AgentDashboard\AgentDashboard.exe
pause
```

- [ ] **Step 2: 确认 `hooks-snippet.json` 无需改动**

`/hook/*` 已限制回环，而 snippet 里用的正是 `http://127.0.0.1:8787`，符合要求。
唯一要改的是补一个注释性质的说明 —— 在 `README.md` 里写明 hooks 必须指向 `127.0.0.1`
而不是局域网 IP，否则会被 403。

- [ ] **Step 3: 更新 `README.md`**

在「接上 Claude Code」一节后新增：

```markdown
## 配对设备

服务首次启动会在 `%APPDATA%\AgentDashboard\config.json` 生成一个令牌。
`/api/*` 全部需要它，所以直接打开 `http://<IP>:8787` 是看不到数据的。

- **平板**：托盘菜单点「复制平板地址」，粘到平板浏览器打开一次即可，令牌会存进 localStorage
- **ESP32**：托盘菜单点「配对新设备（60 秒）」，然后长按板子上的 GPIO21 三秒

`/hook/*` 只接受本机来源，所以 `hooks-snippet.json` 里的地址必须是 `127.0.0.1`，
换成局域网 IP 会被 403。
```

同时把 README 里「后续可以加的」一节中已实现的「多机汇总」条目删掉，改为说明
本方案是各机各跑 + 设备端配对多台。

- [ ] **Step 4: 打包验证**

Run: `build.bat`
Expected: 生成 `dist\AgentDashboard\AgentDashboard.exe`

双击运行，逐条确认：

1. 托盘图标出现（说明 `--noconsole` 下 uvicorn 没有崩在 `sys.stdout is None` 上）
2. 「打开控制台」能打开页面（说明 `sys._MEIPASS` 下 `static/` 找得到）
3. 无 N 卡机器上 GPU 显示 `—` 而非崩溃

- [ ] **Step 5: 跑全量测试**

Run: `python -m pytest tests/ -v`
Expected: 全绿

- [ ] **Step 6: Commit**

```bash
git add build.bat README.md
git commit -m "chore: 改用 onedir 打包，补充配对说明"
```

---

## 完成标准

全部任务完成后，以下每一条都应成立：

- [ ] `python -m pytest tests/ -v` 全绿
- [ ] `python server.py` 能启动，`curl http://127.0.0.1:8787/` 返回 200（spec §0 缺陷 1 已修）
- [ ] `curl http://127.0.0.1:8787/api/tiny` 返回 401；带令牌返回三行文本（缺陷 2、3 已修）
- [ ] 从局域网另一台机器 `curl` `/hook/prompt` 返回 403
- [ ] 局域网另一台机器能通过 mDNS 发现服务，但 TXT 里没有任何会话内容
- [ ] 平板打开带令牌的地址能看到实时数据，权限询问时顶部条变琥珀
- [ ] 托盘配对窗口内 `POST /api/pair` 返回令牌并弹气泡，窗口外 403
- [ ] `build.bat` 产物双击能跑

## 未包含（转入 ESP32 计划）

`agent_display.ino` 在本计划中**不做任何改动**。它的 `BUZZER_PIN 2` 与本板 I2C SCL
冲突、屏驱注释错误（实为 SH8501B0）等问题，连同整个固件重写，都在
`docs/superpowers/plans/2026-08-18-esp32-display.md` 中处理 —— 那份计划依赖本计划
交付的 `/api/tiny` 与 `/api/pair`。
