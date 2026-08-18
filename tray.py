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
import phrases
import metrics
import sessions

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
    except OSError:
        return "127.0.0.1"        # 没网时别让托盘菜单渲染崩掉
    finally:
        s.close()


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
    last_waiting_ids = set()
    while True:
        try:
            ss = sessions.snapshot(metrics.current())["sessions"]
        except Exception:
            ss = []

        waiting = [s for s in ss if s["state"] == "waiting"]
        state = overall_state(ss)

        icon.icon = make_icon(state)
        if waiting:
            icon.title = f"{phrases.STATE_LABEL['waiting']}：{waiting[0]['name']}"
        elif ss:
            icon.title = f"{len(ss)} 个会话 · {phrases.STATE_LABEL_SHORT.get(state, '')}"
        else:
            icon.title = phrases.TRAY_EMPTY

        # 按会话身份判断，不按数量 —— 一个处理完、另一个进来时数量不变，
        # 用计数比较会把第二个的提醒静默吞掉。
        ids = {s["id"] for s in waiting}
        fresh = ids - last_waiting_ids
        if fresh:
            first = next(s for s in waiting if s["id"] in fresh)
            try:
                icon.notify(first.get("detail", phrases.WAITING_DEFAULT),
                            phrases.STATE_LABEL["waiting"])
            except Exception:
                pass
        last_waiting_ids = ids

        time.sleep(1.5)


def main():
    if already_running():
        webbrowser.open(tablet_url("localhost", PORT, server.CFG.token))
        return

    bc = server.start_background()

    import uvicorn
    threading.Thread(
        target=lambda: uvicorn.run(server.app, host="0.0.0.0", port=PORT,
                                   log_config=None, access_log=False),
        daemon=True).start()

    icon = pystray.Icon(
        APP_NAME, make_icon("off"), "Agent 控制台",
        menu=pystray.Menu(
            pystray.MenuItem("打开控制台",
                             lambda: webbrowser.open(tablet_url("localhost", PORT, server.CFG.token)),
                             default=True),
            pystray.MenuItem(lambda i: f"复制平板地址  {local_ip()}:{PORT}",
                             lambda: copy(tablet_url(local_ip(), PORT, server.CFG.token))),
            pystray.MenuItem("配对新设备（60 秒）",
                             lambda: server.PAIRING.open(seconds=60)),
            pystray.Menu.SEPARATOR,
            pystray.MenuItem("开机自启", toggle_autostart, checked=lambda i: autostart_on()),
            pystray.MenuItem("退出", lambda i: i.stop()),
        ))

    def on_pair(peer: str):
        try:
            icon.notify(f"{peer}", phrases.PAIRED_BALLOON)
        except Exception:
            pass

    server.PAIRING.on_pair = on_pair

    threading.Thread(target=watch, args=(icon,), daemon=True).start()
    try:
        icon.run()
    finally:
        bc.stop()


def copy(text: str):
    subprocess.run("clip", input=text.encode("utf-16le"), shell=True)


if __name__ == "__main__":
    main()
