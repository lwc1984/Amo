#pragma once

/* 与宿主端 phrases.py 的 STATE_LABEL_SHORT 逐字一致。
   改这里必须同步改 phrases.py，tests/test_firmware_copy.py 会挡住漂移。 */
#define UI_S_WAITING  "该你了"
#define UI_S_RUNNING  "干着呢"
#define UI_S_BUSY     "憋大招"
#define UI_S_IDLE     "摸鱼中"
#define UI_S_STALE    "没声儿了"

/* 连不上任何主机 —— 与"某个会话失联"是两回事，不得合并 */
#define UI_S_NOLINK   "连不上"

#define UI_S_PAIRING  "配对中"
#define UI_S_PAIR_OK  "配上了"
#define UI_S_PAIR_NO  "没找着"
#define UI_S_NOHOST   "还没配对"
