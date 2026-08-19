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

# 一个 running 会话安静多久算什么。
#
# 只用一个阈值是不够的：PostToolUse 要等命令结束才触发，所以一次十分钟的构建
# 期间，会话看起来和卡死一模一样。把两者混为一谈的代价是双向的 —— 要么频繁
# 误报失联（然后你学会无视它），要么把阈值调高到真失联时也不吭声。
#
# 分成两段：安静超过 QUIET 是「憋大招」（在干重活，不用管），
# 超过 STALE 才是「没声儿了」（该去看看了）。
QUIET_TIMEOUT = 90
STALE_TIMEOUT = 600

# 兼容旧名字。曾经只有这一个阈值，含义相当于现在的 QUIET_TIMEOUT。
IDLE_TIMEOUT = QUIET_TIMEOUT
ARG_MAX = 60          # detail 里工具参数的显示上限，超出截断
SESSIONS: dict[str, dict] = {}

# 排序权威。busy 排在 running 之前：它更少见、也更值得先看一眼
# ——「憋了十分钟大招」比「刚敲下回车」信息量大。
_ORDER = {"waiting": 0, "stale": 1, "busy": 2, "running": 3, "idle": 4}


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
    """detail 是"当前在干嘛"，不是摘要。full=False 时脱敏到工具名。

    参数要压成一行并截断：一条多行的 PowerShell 脚本会把卡片撑爆，
    而这一行的用途只是"它卡在哪个命令上"，不是把命令原样重放。
    """
    tool = s.get("tool") or ""
    if not tool:
        return s.get("phrase", "")
    arg = " ".join((s.get("arg") or "").split())
    if len(arg) > ARG_MAX:
        arg = arg[:ARG_MAX] + "…"
    return f"{tool}: {arg}" if (full and arg) else tool


def snapshot(metrics: dict, now: float | None = None) -> dict:
    now = time.time() if now is None else now
    out = []
    for s in list(SESSIONS.values()):
        last = s.get("last_event", now)
        quiet = now - last
        state = s.get("state", "idle")
        if state == "running":
            if quiet > STALE_TIMEOUT:
                state = "stale"
            elif quiet > QUIET_TIMEOUT:
                state = "busy"
        out.append({**s, "state": state,
                    "detail": detail(s, full=True),
                    "age": round(now - last)})
    out.sort(key=lambda x: (_ORDER.get(x["state"], 4), x["name"]))
    return {"metrics": metrics, "sessions": out, "ts": now, "host": HOST}
