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
#include "poller.h"
#include "tds3_board.h"
#include "ui.h"

static const char *TAG = "amo";

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

    if ((err = tds3_lvgl_start()) != ESP_OK) {
        ESP_LOGE(TAG, "LVGL 起不来: %s", esp_err_to_name(err));
        return;
    }
    ui_init();
    tds3_backlight(60);            /* 画完第一帧再开背光，避免上电闪白 */

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

    poller_start(&s_hosts);

    /* 一个循环三种节奏，用计数分频，不另开任务：
       按键 50ms（再慢会漏掉快速短按），UI 250ms（够跟手，又不至于让 LVGL
       每帧重排），日志 3 秒（串口刷太快反而看不清）。 */
    uint32_t beat = 0, ticks = 0;
    view_t v;
    bool have_view = false;

    while (1) {
        btn_event_t e = buttons_poll();
        if (e == BTN_LONG) {
            do_pairing();
        } else if (e == BTN_SHORT) {
            hosts_cycle(&s_hosts);
            const host_t *c = hosts_current(&s_hosts);
            ESP_LOGI(TAG, "切换到 %s", c ? c->name : "(无)");
        }

        ticks++;

        if (ticks % 5 == 0) {              /* 250ms：刷屏 */
            if (poller_take(&v)) {
                have_view = true;
                ui_render(&v);
                /* waiting 是唯一允许喊叫的状态：背光同时拉满。
                   LCD 背光全屏均匀，画黑不省电，分档只能靠 PWM 。 */
                tds3_backlight(v.state == VS_WAITING ? 100 : 55);
            }
        }

#ifdef CONFIG_AGENT_DUMP_FRAME_ON_BOOT
        /* 开机 18 秒时 dump 一帧。人不在板子旁边时，这是唯一能验证排版的办法。
           只在有视图之后 dump，否则抓到的是还没画东西的空屏。 */
        if (beat == 5 && ticks == 55 && have_view) {
            ESP_LOGI(TAG, "dump 一帧供主机侧还原");
            tds3_dump_frame();
        }
#endif

        if (ticks >= 60) {                 /* 3 秒：日志 */
            ticks = 0;
            if (have_view) {
                static const char *SN[] = {"连不上", "摸鱼中", "干着呢", "该你了", "没声儿了"};
                ESP_LOGI(TAG, "[%lu] %s | %s | %s | %s | cpu%d mem%d gpu%d net%d%s%s | 堆 %lu",
                         (unsigned long)++beat, SN[v.state], v.host,
                         v.name[0] ? v.name : "(无会话)",
                         v.detail[0] ? v.detail : "-",
                         v.cpu, v.mem, v.gpu, v.net_kb,
                         v.peer_needs_you ? " | 另一台在等: " : "",
                         v.peer_needs_you ? v.peer_name : "",
                         (unsigned long)esp_get_free_heap_size());
            } else {
                ESP_LOGW(TAG, "[%lu] 还没拿到任何视图", (unsigned long)++beat);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
