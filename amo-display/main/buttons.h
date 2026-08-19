#pragma once

typedef enum { BTN_NONE, BTN_SHORT, BTN_LONG } btn_event_t;

/* T-Display S3 的可用按键是 GPIO14。GPIO0 是 BOOT strapping 脚，不碰 ——
   上电时它的电平决定进不进下载模式。 */
void buttons_init(void);

/* 每 50ms 调一次 */
btn_event_t buttons_poll(void);
