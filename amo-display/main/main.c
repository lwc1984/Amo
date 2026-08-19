/* 最小 main：点亮屏幕，画出四种状态色的色带。
 *
 * 色带不只是"证明能画" —— 它同时验证了设计里那条铁律在真实硬件上成立：
 * 颜色只编码状态。四条色带自上而下是 run / wait / idle / stale，
 * 与托盘、网页、phrases.py 用的是同一组值。任何一处改了颜色，四块屏必须一起改。
 *
 * 同时色带是上下不对称的，可以一眼确认横屏方向对不对。
 */
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "mdns.h"

#include "buttons.h"
#include "discovery.h"
#include "hosts.h"
#include "hosts_nvs.h"
#include "net_wifi.h"
#include "pairing.h"
#include "tds3_board.h"

static const char *TAG = "amo";

#define RGB565(r, g, b) \
    ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

/* 诊断用的原色。不用设计色板 —— 相机白平衡会把琥珀拍成绿色，
   而纯红/纯绿/纯蓝/纯白在任何白平衡下都认得出来。 */
#define C_RED   RGB565(0xFF, 0x00, 0x00)
#define C_GREEN RGB565(0x00, 0xFF, 0x00)
#define C_BLUE  RGB565(0x00, 0x00, 0xFF)
#define C_WHITE RGB565(0xFF, 0xFF, 0xFF)
#define C_BLACK RGB565(0x00, 0x00, 0x00)

/* 与 tray.py:COLORS、static/index.html 的 CSS 变量逐值一致 */
#define COLOR_RUN   RGB565(0x3F, 0xBF, 0xD8)   /* #3FBFD8 干着呢 */
#define COLOR_WAIT  RGB565(0xFF, 0xB0, 0x20)   /* #FFB020 该你了 */
#define COLOR_IDLE  RGB565(0x4A, 0x5B, 0x78)   /* #4A5B78 摸鱼中 */
#define COLOR_STALE RGB565(0xE2, 0x56, 0x4A)   /* #E2564A 没声儿了 */

#define BAND_H (TDS3_HEIGHT / 4)

/* 整屏帧缓冲。在内存里把一帧画完整，再一次性发给屏幕。
 *
 * 之前是每个色块单独调一次 tds3_blit —— 每次都要重设一遍绘制窗口
 * (CASET/RASET)，八次窗口切换里出现了确定性的列错位。一帧一次传输
 * 既绕开了这个问题，也是最终 UI 本来就该用的方式。
 * 320*170*2 = 108,800 字节，必须是 DMA 可达的内部 RAM。 */
static uint16_t *s_fb = NULL;

static void fb_rect(int x0, int y0, int x1, int y1, uint16_t color)
{
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > TDS3_WIDTH) x1 = TDS3_WIDTH;
    if (y1 > TDS3_HEIGHT) y1 = TDS3_HEIGHT;
    for (int y = y0; y < y1; y++) {
        uint16_t *row = s_fb + (size_t)y * TDS3_WIDTH;
        for (int x = x0; x < x1; x++) {
            row[x] = color;
        }
    }
}

/* 四条状态色带，外加一个左上角白方块确认方向。
 *
 * 色带不只是"证明能画" —— 它验证了设计里那条铁律在真实硬件上成立：
 * 颜色只编码状态。四条自上而下是 run / wait / idle / stale，与 tray.py:COLORS
 * 和 static/index.html 的 CSS 变量逐值一致。任何一处改颜色，四块屏必须一起改。
 */
static void draw_bands(void)
{
    static const uint16_t colors[4] = {COLOR_RUN, COLOR_WAIT, COLOR_IDLE, COLOR_STALE};
    const int bh = TDS3_HEIGHT / 4;

    for (int i = 0; i < 4; i++) {
        int y1 = (i == 3) ? TDS3_HEIGHT : (i + 1) * bh;
        fb_rect(0, i * bh, TDS3_WIDTH, y1, colors[i]);
    }
    fb_rect(4, 4, 24, 24, C_WHITE);        /* 左上角原点标记 */

    esp_err_t err = tds3_blit(0, 0, TDS3_WIDTH, TDS3_HEIGHT, s_fb);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "整屏传输失败: %s", esp_err_to_name(err));
    }
}

static hosts_t s_hosts;

/* 长按触发：扫 mDNS，对每台没配对过的主机试一次 /api/pair。
 *
 * 会对**所有**发现到的主机发起请求，包括同事的机器 —— 但那没有风险：
 * 对方没点开配对窗口就是 403，且每次成功配对都会在对方机器上弹一次气泡。
 * 想偷偷配上别人的机器，需要恰好在对方主动开窗口的 60 秒内动手，而且对方会看见。
 */
static void do_pairing(void)
{
    ESP_LOGI(TAG, "开始配对扫描");
    host_t found[HOSTS_MAX];
    int n = discovery_scan(found, HOSTS_MAX, 3000);
    int added = 0;

    for (int i = 0; i < n; i++) {
        if (hosts_find(&s_hosts, found[i].host_id) >= 0) {
            /* 已配对过，只更新地址。令牌不动 —— DHCP 换个 IP 不该让人重配一次。 */
            hosts_set_addr(&s_hosts, found[i].host_id, found[i].ip, found[i].port);
            ESP_LOGI(TAG, "%s 已配对，更新地址为 %s:%d",
                     found[i].name, found[i].ip, found[i].port);
            continue;
        }

        char token[40];
        if (pairing_request(found[i].ip, found[i].port, token, sizeof(token))) {
            host_t h = found[i];
            snprintf(h.token, sizeof(h.token), "%s", token);
            if (hosts_upsert(&s_hosts, &h)) {
                added++;
                ESP_LOGI(TAG, "配对成功: %s", h.name);
            } else {
                ESP_LOGW(TAG, "主机表满了（上限 %d），%s 没能加进去",
                         HOSTS_MAX, h.name);
            }
        }
    }

    if (n > 0) {
        esp_err_t err = hosts_nvs_save(&s_hosts);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "存 NVS 失败: %s", esp_err_to_name(err));
        }
    }
    ESP_LOGI(TAG, "配对结束，新增 %d 台，共 %d 台", added, s_hosts.count);
}

void app_main(void)
{
    ESP_LOGI(TAG, "启动");

    esp_err_t err = tds3_board_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "屏幕起不来: %s", esp_err_to_name(err));
        return;
    }

    s_fb = heap_caps_malloc((size_t)TDS3_WIDTH * TDS3_HEIGHT * sizeof(uint16_t),
                            MALLOC_CAP_DMA);
    if (!s_fb) {
        ESP_LOGE(TAG, "帧缓冲分配失败，需要 %d 字节 DMA 内存",
                 TDS3_WIDTH * TDS3_HEIGHT * (int)sizeof(uint16_t));
        return;
    }
    ESP_LOGI(TAG, "帧缓冲 %d 字节已分配", TDS3_WIDTH * TDS3_HEIGHT * (int)sizeof(uint16_t));

    draw_bands();
    tds3_backlight(true);          /* 画完再开背光，避免上电闪白 */
    ESP_LOGI(TAG, "四条状态色带已画：run 青蓝 / wait 琥珀 / idle 深蓝灰 / stale 砖红");

    /* 屏幕先亮起来再连网 —— 连不上时至少能看见东西，而不是一块黑板。 */
    if (net_wifi_start() == ESP_OK) {
        /* mdns_init() 必须在 net_wifi_start() 之后。它依赖 esp_netif_init() 与
           默认事件循环，而那两样是在 net_wifi_start() 里建的。顺序反了会返回
           ESP_FAIL —— 报错只说 mdns_init 失败，完全不提是缺了 netif。 */
        ESP_ERROR_CHECK(mdns_init());
        ESP_LOGI(TAG, "WiFi 已连接，开始 mDNS 发现");
        host_t found[HOSTS_MAX];
        int n = discovery_scan(found, HOSTS_MAX, 3000);
        for (int i = 0; i < n; i++) {
            ESP_LOGI(TAG, "发现 %s (%s) @ %s:%d",
                     found[i].name, found[i].host_id, found[i].ip, found[i].port);
        }
        if (n == 0) {
            ESP_LOGW(TAG, "一台都没发现。确认宿主端在跑，且板子与它同一子网。");
        }
    } else {
        ESP_LOGE(TAG, "WiFi 连不上。检查 sdkconfig 里的 SSID/密码，以及是否 2.4GHz。");
    }

    buttons_init();
    hosts_nvs_load(&s_hosts);
    {
        const host_t *c = hosts_current(&s_hosts);
        ESP_LOGI(TAG, "已配对 %d 台，当前 %s。长按 GPIO14 三秒配对新主机。",
                 s_hosts.count, c ? c->name : "(无)");
    }

    /* 按键要 50ms 轮询，心跳每 5 秒打一次 —— 用计数分频，不另开任务。 */
    uint32_t beat = 0, ticks = 0;
    while (1) {
        btn_event_t e = buttons_poll();
        if (e == BTN_LONG) {
            do_pairing();
        } else if (e == BTN_SHORT) {
            hosts_cycle(&s_hosts);
            const host_t *c = hosts_current(&s_hosts);
            ESP_LOGI(TAG, "切换到 %s", c ? c->name : "(无)");
        }

        if (++ticks >= 100) {              /* 100 * 50ms = 5 秒 */
            ticks = 0;
            ESP_LOGI(TAG, "心跳 %lu | 屏 %dx%d | WiFi %s | 已配对 %d 台 | 空闲堆 %lu",
                     (unsigned long)++beat, TDS3_WIDTH, TDS3_HEIGHT,
                     net_wifi_is_up() ? "已连" : "断开", s_hosts.count,
                     (unsigned long)esp_get_free_heap_size());
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
