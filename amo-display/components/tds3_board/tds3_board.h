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

/* 背光开关。本板背光是纯 GPIO 开关，没有硬件调光。 */
void tds3_backlight(bool on);
