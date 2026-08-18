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
TRAY_EMPTY = "没有会话"
CONN_ON = "连上了"
CONN_OFF = "断了，正重连"
ARM_BUTTON = "叫醒我"
ARMED_BUTTON = "盯着呢"
UNPAIRED = "还没配对。在宿主机托盘里点『配对新设备』，然后从那儿复制地址过来。"
TOKEN_STALE = "令牌失效了。去宿主机托盘重新点『配对新设备』，再把地址复制过来。"
PAIRED_BALLOON = "刚配对了一台设备"

# 配对窗口的三处反馈。点了菜单屏幕上什么都不发生的话，
# 用户无法判断是自己没点中、还是功能坏了、还是本来就没有可见效果。
PAIRING_MENU = "配对新设备（60 秒）"
PAIRING_OPEN = "配对窗口开着"
PAIRING_OPENED = "窗口开了。60 秒内让设备来要令牌。"
