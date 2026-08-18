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
