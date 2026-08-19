#pragma once
/* LilyGo T-Display S3 板级支持。
 *
 * ST7789 LCD 170x320，8 位并口(I8080)。本项目横屏用，对外就是 320x170。
 * 这块板没有 PMU、没有板载 RGB 灯 —— 屏幕电源由 GPIO15 直接使能。
 *
 * 驱动本身是 ESP-IDF 内置的 esp_lcd + esp_lcd_new_panel_st7789，
 * 所以这里只有引脚定义和初始化顺序，没有从上游拷来的代码。
 */
#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_lcd_panel_ops.h"

/* 横屏后的可视尺寸 */
#define TDS3_WIDTH   320
#define TDS3_HEIGHT  170

/* 两个按键。BOOT 键复用为 GPIO0。 */
#define TDS3_BTN_BOOT  0
#define TDS3_BTN_2    14

/* 上电、初始化面板、开背光。失败时返回具体的 esp_err_t。 */
esp_err_t tds3_board_init(void);

/* 初始化之后才有效 */
esp_lcd_panel_handle_t tds3_panel(void);

/* 同步绘制一块像素，返回时 DMA 已经搬完，调用方可以安全改写/释放 px。
 *
 * 必须走这个接口，不要直接调 esp_lcd_panel_draw_bitmap() —— 后者是异步的，
 * 排进队列就返回，DMA 还在读你的缓冲区。画完就 free 或者接着填下一块颜色，
 * 屏幕上会出现串色和整行错位，而且时好时坏。 */
esp_err_t tds3_blit(int x0, int y0, int x1, int y1, const void *px);

/* 背光。0-100 的占空比，走 LEDC PWM。
   LCD 背光是全屏均匀的，画成黑色一分电都不省 —— 省电只能靠调这个，
   这是相对 AMOLED 方案的实质差别，不是换个数字的事。 */
void tds3_backlight(int percent);

/* 把 LVGL 接到这块屏上：建绘制缓冲、注册显示驱动、起 tick 与 handler 任务。
   调用之后不要再用 tds3_blit —— 刷屏由 LVGL 接管，两条路径抢同一个 DMA。 */
esp_err_t tds3_lvgl_start(void);

/* 把当前屏幕内容以 base64 的 RGB565 吐到串口，供主机侧还原成图片。
 *
 * 存在的理由很实际：开发时人不在板子旁边，或者根本看不到屏幕，就没法验证
 * 排版。有了它，"UI 长什么样"从一个只能靠肉眼的问题变成可自动检查的问题。
 * 影子缓冲放 PSRAM，不占 DMA 可达的内部 RAM，也不改渲染路径。 */
void tds3_dump_frame(void);
