"""
Agent 控制台 — Windows 托盘宿主
依赖: pip install pystray pillow
打包: 见 build.bat
"""
import ctypes, socket, subprocess, sys, threading, time, webbrowser
import winreg
from pathlib import Path

from PIL import Image, ImageDraw
import pystray

import server                      # 复用 server.py 的 app / snapshot

PORT = server.PORT
RUN_KEY = r"Software\Microsoft\Windows\CurrentVersion\Run"
APP_NAME = "AgentDashboard"

# 与平板 / ESP32 完全相同的语义色
COLORS = {"waiting": (255, 176, 32), "running": (63, 191, 216),
          "idle": (74, 91, 120), "stale": (226, 86, 74), "off": (60, 66, 78)}


# ── 单实例：拿不到端口就说明已在运行 ────────────────────────
def already_running() -> bool:
    s = socket.socket()
    try:
        s.bind(("127.0.0.1", PORT))
        s.close()
        return False
    except OSError:
        return True


# ── 动态画图标，不用打包 .ico ────────────────────────────────
def make_icon(state: str, count: int = 0) -> Image.Image:
    img = Image.new("RGBA", (64, 64), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    c = COLORS.get(state, COLORS["off"])
    d.ellipse((10, 10, 54, 54), outline=c + (255,), width=6)
    if state in ("waiting", "stale"):
        d.ellipse((22, 22, 42, 42), fill=c + (255,))       # 实心 = 需要你
    elif state == "running":
        d.ellipse((26, 26, 38, 38), fill=c + (255,))
    return img


def local_ip() -> str:
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("223.5.5.5", 80))
        return s.getsockname()[0]
    finally:
        s.close()


# ── 开机自启 ─────────────────────────────────────────────────
def autostart_on() -> bool:
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, RUN_KEY) as k:
            winreg.QueryValueEx(k, APP_NAME)
        return True
    except FileNotFoundError:
        return False


def toggle_autostart(icon, item):
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER, RUN_KEY, 0, winreg.KEY_SET_VALUE) as k:
        if autostart_on():
            winreg.DeleteValue(k, APP_NAME)
        else:
            winreg.SetValueEx(k, APP_NAME, 0, winreg.REG_SZ, f'"{sys.executable}"')


# ── 托盘状态刷新 ─────────────────────────────────────────────
def watch(icon: pystray.Icon):
    last_waiting = 0
    while icon.visible or True:
        try:
            ss = server.snapshot()["sessions"]
        except Exception:
            ss = []

        waiting = [s for s in ss if s["state"] == "waiting"]
        if waiting:
            state = "waiting"
        elif any(s["state"] == "stale" for s in ss):
            state = "stale"
        elif any(s["state"] == "running" for s in ss):
            state = "running"
        elif ss:
            state = "idle"
        else:
            state = "off"

        icon.icon = make_icon(state)
        icon.title = (f"需要你处理: {waiting[0]['name']}" if waiting
                      else f"{len(ss)} 个会话 · {state}")

        # 只在"从无到有"时弹一次气泡，不重复骚扰
        if len(waiting) > last_waiting:
            try:
                icon.notify(waiting[0].get("detail", "等待你的操作"), "需要你处理")
            except Exception:
                pass
        last_waiting = len(waiting)

        time.sleep(1.5)


def main():
    if already_running():
        webbrowser.open(f"http://localhost:{PORT}")
        return

    import uvicorn
    threading.Thread(
        target=lambda: uvicorn.run(server.app, host="0.0.0.0", port=PORT,
                                   log_config=None, access_log=False),
        daemon=True).start()

    icon = pystray.Icon(
        APP_NAME, make_icon("off"), "Agent 控制台",
        menu=pystray.Menu(
            pystray.MenuItem("打开控制台", lambda: webbrowser.open(f"http://localhost:{PORT}"),
                             default=True),
            pystray.MenuItem(lambda i: f"平板地址  {local_ip()}:{PORT}",
                             lambda: copy(f"http://{local_ip()}:{PORT}")),
            pystray.Menu.SEPARATOR,
            pystray.MenuItem("开机自启", toggle_autostart, checked=lambda i: autostart_on()),
            pystray.MenuItem("退出", lambda i: i.stop()),
        ))

    threading.Thread(target=watch, args=(icon,), daemon=True).start()
    icon.run()


def copy(text: str):
    subprocess.run("clip", input=text.encode("utf-16le"), shell=True)


if __name__ == "__main__":
    main()
