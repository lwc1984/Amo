"""Agent 控制台 —— 宿主机服务端。

启动: python server.py    监听 0.0.0.0:8787
"""
import asyncio
import io
import json
import sys
from pathlib import Path

import qrcode
import qrcode.image.svg
from fastapi import Depends, FastAPI, HTTPException, Request
from fastapi.responses import FileResponse, PlainTextResponse, StreamingResponse
from fastapi.staticfiles import StaticFiles

import config
import metrics
import phrases
import security
import sessions
import tiny

PORT = 8787

CFG = config.load_config()
PAIRING = security.PairingWindow()


def _notify_pair(peer: str) -> None:
    """默认的配对通知：打到控制台。托盘会用气泡覆盖它。"""
    print(f"{phrases.PAIRED_BALLOON}: {peer}")


PAIRING.on_pair = _notify_pair


def start_background():
    """两个入口点共用的后台启动，返回 mDNS 广播句柄（失败时为 None）。

    以前 mDNS 只在 server.py 的 __main__ 里起、配对窗口只有托盘菜单能开，
    结果打包出的 exe 能配对但不广播、python server.py 能广播但永远配不了对。
    两边都走这里，就不会再分裂成两个半成品。
    """
    import threading
    import time

    import discovery

    metrics.start_collector()

    bc = discovery.Broadcast(PORT, CFG.host_id, sessions.HOST)
    try:
        bc.start()
    except Exception as e:                  # 没网 / 端口占用都不该拦住主服务
        print(f"mDNS 广播启动失败，设备得手填地址: {e}")

    def _follow_ip():
        """宿主 IP 变了就重新广播 —— 这正是当初选 mDNS 的理由。"""
        while True:
            time.sleep(30)
            try:
                bc.refresh()
            except Exception as e:
                print(f"mDNS 重新广播失败: {e}")

    threading.Thread(target=_follow_ip, daemon=True).start()
    return bc


def resource_path(rel: str) -> Path:
    """PyInstaller 打包后资源解压在 sys._MEIPASS，源码运行时就是脚本目录。"""
    return Path(getattr(sys, "_MEIPASS", Path(__file__).parent)) / rel


STATIC = resource_path("static")

app = FastAPI(docs_url=None, redoc_url=None, openapi_url=None)


async def require_token(request: Request) -> None:
    provided = request.query_params.get("k") or request.headers.get("X-Agent-Key")
    if not security.check_token(provided, CFG.token):
        raise HTTPException(401, "需要有效令牌")


async def require_loopback(request: Request) -> None:
    host = request.client.host if request.client else None
    if not security.is_loopback(host):
        raise HTTPException(403, "hook 只接受本机来源")


@app.post("/hook/{event}", dependencies=[Depends(require_loopback)])
async def hook(event: str, request: Request, kind: str = ""):
    """Claude Code 的 http hook 全部打到这里。必须快速返回 200。"""
    try:
        payload = await request.json()
    except Exception:
        payload = {}
    # matcher 类型从查询参数来：Notification 的 payload 里不一定带得上，
    # 而 permission_prompt / idle_prompt 的区分是告警不变成狼来了的前提。
    if kind and not payload.get("notification_type"):
        payload["notification_type"] = kind
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
    """d=full 给完整参数，d=tool 强制脱敏；不带 d 时按 config 的 tiny_detail 决定。"""
    snap = sessions.snapshot(metrics.current())
    full = (d == "full") if d else (CFG.tiny_detail == "full")
    return tiny.render_tiny(snap, full=full)


@app.post("/api/pair")
async def pair(request: Request):
    if not PAIRING.is_open():
        raise HTTPException(403, "配对窗口没开")
    PAIRING.record(request.client.host if request.client else "?")
    return {"token": CFG.token, "host_id": CFG.host_id, "host": sessions.HOST}


def pair_url(ip: str | None = None) -> str:
    """平板要打开的完整地址。令牌走查询参数，页面收到后存进 localStorage 再抹掉地址栏。"""
    import discovery

    return f"http://{ip or discovery.local_ip()}:{PORT}/?k={CFG.token}"


@app.get("/api/qr", dependencies=[Depends(require_loopback)])
async def qr_endpoint():
    """配对二维码，只对回环开放。

    图里编的是带令牌的地址。页面外壳本身不需要令牌就发给整个局域网，所以二维码
    一旦对局域网可见，同网段任何设备扫一下就绕过了 60 秒配对窗口。这个 403 是
    整个功能的前提，不是可以放宽的细节。
    """
    url = pair_url()
    img = qrcode.make(url, image_factory=qrcode.image.svg.SvgPathImage)
    buf = io.BytesIO()
    img.save(buf)
    # 砍掉 XML 前言：这段 SVG 是用 innerHTML 塞进 HTML 文档的，
    # <?xml ...?> 在 HTML 解析器眼里是个伪注释节点，纯属垃圾。
    svg = buf.getvalue().decode("utf-8")
    svg = svg[svg.index("<svg"):]
    # 明文地址一并给出：页面是从 localhost 打开的，自己不知道宿主的局域网 IP，
    # 而扫不动时的手抄兜底行必须显示完整地址。
    return {"url": url, "svg": svg}


@app.post("/api/pair/open", dependencies=[Depends(require_loopback)])
async def pair_open():
    """给没有托盘的运行方式开配对窗口。只接受本机来源，和 /hook/* 同一信任级别。"""
    PAIRING.open(seconds=60)
    return {"open": True, "seconds": 60}


@app.get("/")
async def index():
    return FileResponse(STATIC / "index.html")


app.mount("/static", StaticFiles(directory=STATIC), name="static")


if __name__ == "__main__":
    import uvicorn

    bc = start_background()
    try:
        uvicorn.run(app, host="0.0.0.0", port=PORT, log_level="warning")
    finally:
        bc.stop()
