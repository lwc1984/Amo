#pragma once
#include <stdbool.h>

typedef enum { BTN_NONE, BTN_SHORT, BTN_LONG } btn_event_t;

/* T-Display S3 的可用按键是 GPIO14。GPIO0 是 BOOT strapping 脚，不碰 ——
   上电时它的电平决定进不进下载模式。 */
void buttons_init(void);

/* 每 50ms 调一次 */
btn_event_t buttons_poll(void);

/* 开机时按键是否被持续按住 hold_ms 毫秒。中途松开立刻返回 false。
   用于"开机按住清空配对" —— 要求持续按住而不是按一下，是因为这个操作
   不可撤销：清掉之后必须重新走一遍配对窗口才能再读到任何数据。 */
bool buttons_held_at_boot(int hold_ms);
