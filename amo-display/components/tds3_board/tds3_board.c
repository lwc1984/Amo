#include "tds3_board.h"

#include "driver/gpio.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "tds3";

#define PIN_POWER_ON 15   /* 板子总电源使能。不拉高的话屏幕完全不工作。 */
#define PIN_LCD_BL   38   /* 背光 */
#define PIN_LCD_RD    9
#define PIN_LCD_WR    8
#define PIN_LCD_DC    7
#define PIN_LCD_CS    6
#define PIN_LCD_RES   5

/* 面板物理宽 170，而 ST7789 控制器是 240 宽，起始列偏移 35。
   横屏（swap_xy）之后这个偏移落在 y 方向。 */
#define PANEL_GAP 35

static esp_lcd_panel_handle_t s_panel = NULL;
static esp_lcd_panel_io_handle_t s_io = NULL;
static SemaphoreHandle_t s_done = NULL;

/* 颜色传输完成回调。i80 驱动在 DMA 真正搬完之后才调它。 */
static bool IRAM_ATTR on_trans_done(esp_lcd_panel_io_handle_t io,
                                    esp_lcd_panel_io_event_data_t *ed, void *ctx)
{
    BaseType_t hp = pdFALSE;
    xSemaphoreGiveFromISR(s_done, &hp);
    return hp == pdTRUE;
}

esp_err_t tds3_blit(int x0, int y0, int x1, int y1, const void *px)
{
    esp_err_t err = esp_lcd_panel_draw_bitmap(s_panel, x0, y0, x1, y1, px);
    if (err != ESP_OK) {
        return err;
    }
    /* 等 DMA 真搬完再返回。少了这一步，调用方一 free 或一改缓冲区，
       正在传输的内容就被换掉了。 */
    xSemaphoreTake(s_done, portMAX_DELAY);
    return ESP_OK;
}

/* 厂商专属初始化表，抄自 LilyGo-Display-IDF 的 display_s3.c。
 *
 * IDF 自带的 esp_lcd_new_panel_st7789() 只做通用初始化，不发这些。
 * 缺了它们面板跑在出厂默认的伽马和电压上，症状是颜色不准 —— 纯蓝发白、
 * 混合色偏移，但红/绿/黑看着又是对的，很容易被误判成"驱动没问题"。
 *
 * 0x3A 像素格式，0xB2 门廊时序，0xB7 门极，0xBB VCOM，0xC0-0xC6 电压/VDV/VRH，
 * 0xE0/0xE1 是两条 14 字节的正负伽马曲线 —— 颜色观感主要由它们决定。
 * len 的最高位是"发完延时 120ms"的标志，低 7 位才是真实长度。 */
typedef struct {
    uint8_t addr;
    uint8_t param[16];
    uint8_t len;
} vendor_cmd_t;

static const vendor_cmd_t ST7789V_INIT[] = {
    {0x11, {0}, 0 | 0x80},
    {0x3A, {0x05}, 1},
    {0xB2, {0x0B, 0x0B, 0x00, 0x33, 0x33}, 5},
    {0xB7, {0x75}, 1},
    {0xBB, {0x28}, 1},
    {0xC0, {0x2C}, 1},
    {0xC2, {0x01}, 1},
    {0xC3, {0x1F}, 1},
    {0xC6, {0x13}, 1},
    {0xD0, {0xA7}, 1},
    {0xD0, {0xA4, 0xA1}, 2},
    {0xD6, {0xA1}, 1},
    {0xE0, {0xF0, 0x05, 0x0A, 0x06, 0x06, 0x03, 0x2B, 0x32, 0x43, 0x36, 0x11, 0x10, 0x2B, 0x32}, 14},
    {0xE1, {0xF0, 0x08, 0x0C, 0x0B, 0x09, 0x24, 0x2B, 0x22, 0x43, 0x38, 0x15, 0x16, 0x2F, 0x37}, 14},
};

esp_lcd_panel_handle_t tds3_panel(void)
{
    return s_panel;
}

void tds3_backlight(bool on)
{
    gpio_set_level(PIN_LCD_BL, on ? 1 : 0);
}

esp_err_t tds3_board_init(void)
{
    esp_err_t err;

    gpio_config_t io = {
        .pin_bit_mask = (1ULL << PIN_POWER_ON) | (1ULL << PIN_LCD_BL) | (1ULL << PIN_LCD_RD),
        .mode = GPIO_MODE_OUTPUT,
    };
    if ((err = gpio_config(&io)) != ESP_OK) return err;

    gpio_set_level(PIN_POWER_ON, 1);
    /* RD 必须保持高。拉低等于告诉 ST7789 主机要读，写就进不去了。 */
    gpio_set_level(PIN_LCD_RD, 1);
    gpio_set_level(PIN_LCD_BL, 0);        /* 先别亮，等画完第一帧再开，避免闪白 */
    vTaskDelay(pdMS_TO_TICKS(100));

    esp_lcd_i80_bus_handle_t bus = NULL;
    esp_lcd_i80_bus_config_t bus_cfg = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .dc_gpio_num = PIN_LCD_DC,
        .wr_gpio_num = PIN_LCD_WR,
        .data_gpio_nums = {39, 40, 41, 42, 45, 46, 47, 48},
        .bus_width = 8,
        /* 必须 >= 单次 draw_bitmap 的最大字节数。设小了驱动会把一次绘制拆成
           多段传输，拆分处在屏幕上留下一道错位缝。按整屏给足。 */
        .max_transfer_bytes = TDS3_WIDTH * TDS3_HEIGHT * sizeof(uint16_t),
        /* DMA 对齐。不设的话部分扫描行会整体偏移若干像素，
           表现为屏幕中部和顶部莫名其妙的横向撕裂。 */
        .psram_trans_align = 64,
        .sram_trans_align = 4,
    };
    if ((err = esp_lcd_new_i80_bus(&bus_cfg, &bus)) != ESP_OK) return err;

    esp_lcd_panel_io_handle_t iohd = NULL;
    (void)0;
    esp_lcd_panel_io_i80_config_t io_cfg = {
        .cs_gpio_num = PIN_LCD_CS,
        .pclk_hz = 10 * 1000 * 1000,
        .trans_queue_depth = 10,
        .dc_levels = { .dc_idle_level = 0, .dc_cmd_level = 0,
                       .dc_dummy_level = 0, .dc_data_level = 1 },
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .on_color_trans_done = on_trans_done,
        /* ESP32 是小端，uint16_t 的 0xF800 在内存里是 00 F8；而 ST7789 按大端
           读 RGB565，会当成 0x00F8。症状是红显示成蓝、绿显示成红、蓝显示成绿 ——
           而白(FFFF)和黑(0000)字节对调后不变，始终正确，非常容易让人误判成
           "颜色通路没问题"，去查几何。
           注意：将来接 LVGL 时，若开了 CONFIG_LV_COLOR_16_SWAP=y，LVGL 已经
           自己吐出对调过的字节，那时这里必须关掉，否则又反回去。 */
        .flags.swap_color_bytes = 1,
    };
    s_done = xSemaphoreCreateBinary();
    if (!s_done) return ESP_ERR_NO_MEM;
    if ((err = esp_lcd_new_panel_io_i80(bus, &io_cfg, &iohd)) != ESP_OK) return err;
    s_io = iohd;

    esp_lcd_panel_dev_config_t pcfg = {
        .reset_gpio_num = PIN_LCD_RES,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    if ((err = esp_lcd_new_panel_st7789(iohd, &pcfg, &s_panel)) != ESP_OK) return err;

    if ((err = esp_lcd_panel_reset(s_panel)) != ESP_OK) return err;
    if ((err = esp_lcd_panel_init(s_panel)) != ESP_OK) return err;
    /* T-Display S3 的面板是反色接法，不翻的话所有颜色都是补色 */
    if ((err = esp_lcd_panel_invert_color(s_panel, true)) != ESP_OK) return err;
    if ((err = esp_lcd_panel_swap_xy(s_panel, true)) != ESP_OK) return err;
    if ((err = esp_lcd_panel_mirror(s_panel, false, true)) != ESP_OK) return err;
    if ((err = esp_lcd_panel_set_gap(s_panel, 0, PANEL_GAP)) != ESP_OK) return err;
    /* 厂商初始化表必须在通用 init 与几何设置之后发 */
    for (size_t i = 0; i < sizeof(ST7789V_INIT) / sizeof(ST7789V_INIT[0]); i++) {
        const vendor_cmd_t *c = &ST7789V_INIT[i];
        err = esp_lcd_panel_io_tx_param(s_io, c->addr, c->param, c->len & 0x7F);
        if (err != ESP_OK) return err;
        if (c->len & 0x80) {
            vTaskDelay(pdMS_TO_TICKS(120));
        }
    }

    if ((err = esp_lcd_panel_disp_on_off(s_panel, true)) != ESP_OK) return err;

    ESP_LOGI(TAG, "屏幕就绪 %dx%d（横屏）", TDS3_WIDTH, TDS3_HEIGHT);
    return ESP_OK;
}
