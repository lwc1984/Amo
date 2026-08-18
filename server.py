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
