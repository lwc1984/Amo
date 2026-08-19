#include "net_wifi.h"

#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"

static const char *TAG = "wifi";

static EventGroupHandle_t s_events;
static volatile bool s_up = false;

#define GOT_IP BIT0

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)data;

    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_up = false;
        /* 一直重连。这是常驻桌面设备，路由器重启或信号短暂丢失之后必须自己回来，
           不能像一次性脚本那样放弃 —— 断连是显示态之一，不是终止条件。 */
        ESP_LOGW(TAG, "断开，重连");
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "拿到 IP " IPSTR, IP2STR(&ev->ip_info.ip));
        s_up = true;
        xEventGroupSetBits(s_events, GOT_IP);
    }
}

bool net_wifi_is_up(void)
{
    return s_up;
}

esp_err_t net_wifi_start(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    s_events = xEventGroupCreate();
    if (!s_events) {
        return ESP_ERR_NO_MEM;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &on_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &on_event, NULL, NULL));

    wifi_config_t wc = {0};
    snprintf((char *)wc.sta.ssid, sizeof(wc.sta.ssid), "%s", CONFIG_AGENT_WIFI_SSID);
    snprintf((char *)wc.sta.password, sizeof(wc.sta.password), "%s", CONFIG_AGENT_WIFI_PASSWORD);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "连接 SSID \"%s\" ...", CONFIG_AGENT_WIFI_SSID);
    EventBits_t bits = xEventGroupWaitBits(s_events, GOT_IP, pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(20000));
    return (bits & GOT_IP) ? ESP_OK : ESP_ERR_TIMEOUT;
}
