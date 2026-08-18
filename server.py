"""
Agent Dashboard - 宿主机服务端
依赖: pip install fastapi uvicorn[standard] psutil nvidia-ml-py
启动: python server.py
"""
import asyncio, json, os, time, threading
from pathlib import Path
from collections import deque

import psutil
from fastapi import FastAPI, Request
from fastapi.responses import StreamingResponse, FileResponse
from fastapi.staticfiles import StaticFiles

PORT = 8787
IDLE_TIMEOUT = 90          # 秒，超过没事件就标记为失联
HISTORY = 60               # 保留 60 个采样点画 sparkline

app = FastAPI()
BASE = Path(__file__).parent

# ---------------- 系统指标采集 ----------------

_metrics = {"cpu": 0, "mem": 0, "disk": 0, "gpu": None, "vram": None,
            "net_up": 0, "net_down": 0,
            "hist": {k: [] for k in ("cpu", "mem", "gpu", "net")}}

try:
    import pynvml
    pynvml.nvmlInit()
    _gpu = pynvml.nvmlDeviceGetHandleByIndex(0)
except Exception:
    _gpu = None


def _collect_loop():
    last = psutil.net_io_counters()
    last_t = time.time()
    hist = {k: deque(maxlen=HISTORY) for k in ("cpu", "mem", "gpu", "net")}

    while True:
        cpu = psutil.cpu_percent(interval=1.0)      # 这一句自带 1 秒阻塞，就是循环节拍
        now, cur = time.time(), psutil.net_io_counters()
        dt = max(now - last_t, 0.001)

        up = (cur.bytes_sent - last.bytes_sent) / dt
        down = (cur.bytes_recv - last.bytes_recv) / dt
        last, last_t = cur, now

        mem = psutil.virtual_memory().percent
        disk = psutil.disk_usage(os.environ.get("SystemDrive", "C:") + "\\").percent

        gpu = vram = None
        if _gpu:
            try:
                gpu = pynvml.nvmlDeviceGetUtilizationRates(_gpu).gpu
                m = pynvml.nvmlDeviceGetMemoryInfo(_gpu)
                vram = round(m.used / m.total * 100)
            except Exception:
                pass

        hist["cpu"].append(cpu)
        hist["mem"].append(mem)
        hist["gpu"].append(gpu or 0)
        hist["net"].append(round((up + down) / 1024))   # KB/s

        _metrics.update(cpu=cpu, mem=mem, disk=disk, gpu=gpu, vram=vram,
                        net_up=round(up), net_down=round(down),
                        hist={k: list(v) for k, v in hist.items()})


threading.Thread(target=_collect_loop, daemon=True).start()

# ---------------- Session 状态 ----------------

SESSIONS: dict[str, dict] = {}


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


def touch(sid: str, payload: dict) -> dict:
    s = SESSIONS.setdefault(sid, {"id": sid, "started": time.time()})
    cwd = payload.get("cwd") or s.get("cwd") or ""
    s["cwd"] = cwd
    s["name"] = payload.get("session_title") or (Path(cwd).name if cwd else sid[:8])
    s["transcript"] = payload.get("transcript_path") or s.get("transcript")
    s["last_event"] = time.time()
    return s


@app.post("/hook/{event}")
async def hook(event: str, request: Request):
    """Claude Code 的 http hook 全部打到这里。必须快速返回 200 + {}。"""
    try:
        payload = await request.json()
    except Exception:
        payload = {}

    sid = payload.get("session_id")
    if not sid:
        return {}

    s = touch(sid, payload)

    if event == "session-start":
        s.update(state="idle", detail="会话已启动")
    elif event == "prompt":
        s.update(state="running", detail="正在思考")
    elif event == "tool":
        tool = payload.get("tool_name", "")
        ti = payload.get("tool_input", {}) or {}
        arg = ti.get("command") or ti.get("file_path") or ti.get("pattern") or ""
        s.update(state="running", detail=f"{tool} {str(arg)[:60]}".strip())
    elif event == "notify":
        # matcher 决定了是哪种通知：permission_prompt / idle_prompt
        kind = (payload.get("notification_type")
                or payload.get("matcher") or "").lower()
        msg = payload.get("message", "")
        if "permission" in kind or "permission" in msg.lower():
            s.update(state="waiting", detail=msg or "等待你批准操作")
        else:
            s.update(state="idle", detail=msg or "等待你的下一条指令")
    elif event == "stop":
        s.update(state="idle", detail=tail_summary(s.get("transcript")) or "已完成")
    elif event == "session-end":
        SESSIONS.pop(sid, None)

    if s.get("state") == "idle" and not s.get("summary"):
        s["summary"] = tail_summary(s.get("transcript"))
    return {}


def snapshot() -> dict:
    now = time.time()
    out = []
    for s in list(SESSIONS.values()):
        stale = now - s.get("last_event", now) > IDLE_TIMEOUT
        out.append({**s,
                    "state": "stale" if (stale and s.get("state") == "running") else s.get("state", "idle"),
                    "age": round(now - s.get("last_event", now))})
    out.sort(key=lambda x: ({"waiting": 0, "running": 1, "idle": 2, "stale": 3}
                            .get(x["state"], 4), x["name"]))
    return {"metrics": _metrics, "sessions": out, "ts": now}


@app.get("/api/stream")
async def stream():
    async def gen():
        while True:
            yield f"data: {json.dumps(snapshot(), ensure_ascii=False)}\n\n"
            await asyncio.sleep(1)
    return StreamingResponse(gen(), media_type="text/event-stream",
                             headers={"Cache-Control": "no-cache",
                                      "X-Accel-Buffering": "no"})


@app.get("/")
async def index():
    return FileResponse(BASE / "static" / "index.html")


app.mount("/static", StaticFiles(directory=BASE / "static"), name="static")

if __name__ == "__main__":
    import uvicorn
    uvicorn.run(app, host="0.0.0.0", port=PORT, log_level="warning")
