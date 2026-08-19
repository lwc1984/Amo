"""给 MCU 的极简端点渲染。

纯文本三行，ESP32 侧按行切分即可，不需要 cJSON。第一行带版本号，
以后加字段不至于把已烧录的固件打死 —— v2 加 stale 计数时正是靠它：
老固件按三个数解析会读错，见到 2 就整体拒绝，显示「连不上」而不是错的状态。

第一行格式：`版本|waiting,running,idle,stale|主机名`

前置条件：`snap["sessions"]` 必须已按 waiting → running → idle → stale 排序，
这由 `sessions.snapshot()` 保证。本模块刻意不重复排序逻辑 —— 排序的唯一权威
在 snapshot()，复制一份会制造两个可能漂移的真相。传入未排序的列表会静默选错
最该显示的那条会话。
"""
import sessions

TINY_VERSION = 2


def render_tiny(snap: dict, full: bool = False) -> str:
    ss = snap["sessions"]

    def n(state: str) -> int:
        return sum(1 for s in ss if s["state"] == state)

    # stale 必须有自己的计数位。它不属于 waiting/running/idle 中的任何一个，
    # 少了这一位，一个会话失联时固件只会看到 0,0,0，把「没声儿了」渲染成
    # 「摸鱼中」—— 正是设计里「断连绝不能渲染成正常」要防的事。
    line1 = (f"{TINY_VERSION}|{n('waiting')},{n('running')},{n('idle')},{n('stale')}"
             f"|{snap['host']}")

    # 依赖 snapshot() 的排序：第一条天然就是最该显示的
    top = ss[0] if ss else None
    line2 = f"{top['name']}\t{sessions.detail(top, full=full)}" if top else ""

    m = snap["metrics"]
    gpu = m.get("gpu")
    net_kb = round((m.get("net_up", 0) + m.get("net_down", 0)) / 1024)
    line3 = (f"{round(m.get('cpu', 0))},{round(m.get('mem', 0))},"
             f"{-1 if gpu is None else round(gpu)},{net_kb}")

    return "\n".join([line1, line2, line3])
