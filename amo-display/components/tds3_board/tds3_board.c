#include "tds3_board.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "lvgl.h"
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

static lv_disp_drv_t s_disp_drv;
static bool s_lvgl_mode = false;

/* 颜色传输完成回调。i80 驱动在 DMA 真正搬完之后才调它。
   LVGL 接管之后要通知 LVGL 而不是放信号量 —— 两条路径抢同一个 DMA，
   同时开着会让 tds3_blit 永远等不到自己的那一次完成。 */
static bool IRAM_ATTR on_trans_done(esp_lcd_panel_io_handle_t io,
                                    esp_lcd_panel_io_event_data_t *ed, void *ctx)
{
    if (s_lvgl_mode) {
        lv_disp_flush_ready(&s_disp_drv);
        return false;
    }
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

/* 背光 PWM。10 位分辨率、5kHz —— 频率要远高于人眼闪烁阈值，
   又不必高到让 LEDC 分辨率被迫下降。 */
#define BL_TIMER   LEDC_TIMER_0
#define BL_CHANNEL LEDC_CHANNEL_0
#define BL_RES     LEDC_TIMER_10_BIT
#define BL_MAX     ((1 << 10) - 1)

static bool s_bl_ready = false;

void tds3_backlight(int percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    if (!s_bl_ready) {
        ledc_timer_config_t tcfg = {
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .duty_resolution = BL_RES,
            .timer_num = BL_TIMER,
            .freq_hz = 5000,
            .clk_cfg = LEDC_AUTO_CLK,
        };
        if (ledc_timer_config(&tcfg) != ESP_OK) return;
        ledc_channel_config_t ccfg = {
            .gpio_num = PIN_LCD_BL,
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel = BL_CHANNEL,
            .timer_sel = BL_TIMER,
            .duty = 0,
            .hpoint = 0,
        };
        if (ledc_channel_config(&ccfg) != ESP_OK) return;
        s_bl_ready = true;
    }

    uint32_t duty = (uint32_t)((BL_MAX * percent) / 100);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, BL_CHANNEL, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BL_CHANNEL);
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
    /* 注意：这里仍用 gpio 直接拉低。tds3_backlight() 第一次被调用时才会把这个脚
       交给 LEDC —— 提前初始化 PWM 会在面板还没初始化完时就点亮背光。 */
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

/* ── LVGL 接入 ─────────────────────────────────────────────── */

/* 绘制缓冲取 1/8 屏高。LVGL 按块渲染再刷，块越大越省传输开销、越费内存；
   1/8 屏（320*22*2 ≈ 14KB）两块共 28KB，在 DMA 可达的内部 RAM 里很宽裕。 */
#define LV_BUF_LINES 22

static lv_disp_draw_buf_t s_draw_buf;

/* 屏幕内容的影子拷贝，放 PSRAM。只为 tds3_dump_frame() 服务。 */
static uint16_t *s_shadow = NULL;

static void lvgl_flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *px)
{
    if (s_shadow) {
        int w = area->x2 - area->x1 + 1;
        const uint16_t *src = (const uint16_t *)px;
        for (int y = area->y1; y <= area->y2 && y < TDS3_HEIGHT; y++) {
            memcpy(s_shadow + (size_t)y * TDS3_WIDTH + area->x1,
                   src + (size_t)(y - area->y1) * w,
                   (size_t)w * sizeof(uint16_t));
        }
    }
    esp_lcd_panel_draw_bitmap(s_panel, area->x1, area->y1,
                              area->x2 + 1, area->y2 + 1, px);
    /* 不在这里调 lv_disp_flush_ready —— DMA 还没搬完。
       由 on_trans_done 回调通知，那才是真正搬完的时刻。 */
}

static void lvgl_tick(void *arg)
{
    (void)arg;
    lv_tick_inc(5);
}

static void lvgl_task(void *arg)
{
    (void)arg;
    for (;;) {
        uint32_t next = lv_timer_handler();
        if (next == LV_NO_TIMER_READY || next > 20) {
            next = 20;
        }
        vTaskDelay(pdMS_TO_TICKS(next < 5 ? 5 : next));
    }
}

esp_err_t tds3_lvgl_start(void)
{
    if (!s_panel) {
        return ESP_ERR_INVALID_STATE;
    }
    lv_init();

    size_t px = (size_t)TDS3_WIDTH * LV_BUF_LINES;
    lv_color_t *b1 = heap_caps_malloc(px * sizeof(lv_color_t), MALLOC_CAP_DMA);
    lv_color_t *b2 = heap_caps_malloc(px * sizeof(lv_color_t), MALLOC_CAP_DMA);
    if (!b1 || !b2) {
        return ESP_ERR_NO_MEM;
    }
    lv_disp_draw_buf_init(&s_draw_buf, b1, b2, px);

    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res = TDS3_WIDTH;
    s_disp_drv.ver_res = TDS3_HEIGHT;
    s_disp_drv.flush_cb = lvgl_flush;
    s_disp_drv.draw_buf = &s_draw_buf;
    if (!lv_disp_drv_register(&s_disp_drv)) {
        return ESP_FAIL;
    }

    /* 注册完再切模式：注册过程本身不会触发传输，但切早了万一有传输在途，
       完成回调会去通知一个还没注册的 disp_drv。 */
    s_lvgl_mode = true;

    const esp_timer_create_args_t targs = {
        .callback = lvgl_tick,
        .name = "lv_tick",
    };
    esp_timer_handle_t th;
    esp_err_t err = esp_timer_create(&targs, &th);
    if (err != ESP_OK) {
        return err;
    }
    if ((err = esp_timer_start_periodic(th, 5000)) != ESP_OK) {
        return err;
    }

    if (xTaskCreate(lvgl_task, "lvgl", 6144, NULL, 3, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    s_shadow = heap_caps_calloc((size_t)TDS3_WIDTH * TDS3_HEIGHT, sizeof(uint16_t),
                                MALLOC_CAP_SPIRAM);
    if (!s_shadow) {
        ESP_LOGW(TAG, "影子缓冲分配失败，tds3_dump_frame 将不可用");
    }

    ESP_LOGI(TAG, "LVGL 已接入，绘制缓冲 2 x %u 字节",
             (unsigned)(px * sizeof(lv_color_t)));
    return ESP_OK;
}

static const char B64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

void tds3_dump_frame(void)
{
    if (!s_shadow) {
        printf("FRAME_ERR no-shadow\n");
        return;
    }
    const uint8_t *b = (const uint8_t *)s_shadow;
    size_t n = (size_t)TDS3_WIDTH * TDS3_HEIGHT * sizeof(uint16_t);

    /* 用 printf 直出而不是 ESP_LOG —— 日志会加时间戳和标签前缀，
       把 base64 流冲得七零八落，主机侧就得去猜哪些字符是数据。 */
    printf("FRAME_BEGIN %d %d rgb565le\n", TDS3_WIDTH, TDS3_HEIGHT);
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t)b[i] << 16;
        int rem = (int)(n - i);
        if (rem > 1) v |= (uint32_t)b[i + 1] << 8;
        if (rem > 2) v |= (uint32_t)b[i + 2];
        char out[5];
        out[0] = B64[(v >> 18) & 63];
        out[1] = B64[(v >> 12) & 63];
        out[2] = rem > 1 ? B64[(v >> 6) & 63] : '=';
        out[3] = rem > 2 ? B64[v & 63] : '=';
        out[4] = 0;
        fputs(out, stdout);
        if ((i / 3) % 24 == 23) {
            fputc(10, stdout);   /* 换行。直接用 ASCII 码，省掉一层转义 */
        }
    }
    printf("\nFRAME_END\n");
    fflush(stdout);
}
