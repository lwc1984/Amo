#include "hosts_nvs.h"

#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "hostsnvs";

#define NS  "agentdash"
#define KEY "hosts"

esp_err_t hosts_nvs_load(hosts_t *h)
{
    hosts_init(h);

    nvs_handle_t nh;
    esp_err_t err = nvs_open(NS, NVS_READONLY, &nh);
    if (err != ESP_OK) {
        return err;                       /* 首次开机没有这个命名空间，正常 */
    }

    size_t len = sizeof(hosts_t);
    err = nvs_get_blob(nh, KEY, h, &len);
    nvs_close(nh);

    /* 下面三道检查是"不信任 flash 里的内容"。结构体改过、掉电写坏、
       或者别的固件写过同一个键，都会让这里读到不合法的值；
       与其带着坏数据跑下去，不如清空重来。 */
    if (err != ESP_OK || len != sizeof(hosts_t)) {
        hosts_init(h);
        return ESP_ERR_INVALID_SIZE;
    }
    if (h->count < 0 || h->count > HOSTS_MAX) {
        hosts_init(h);
        return ESP_ERR_INVALID_STATE;
    }
    if (h->current < 0 || h->current >= h->count) {
        h->current = 0;
    }
    ESP_LOGI(TAG, "载入 %d 台已配对主机", h->count);
    return ESP_OK;
}

esp_err_t hosts_nvs_save(const hosts_t *h)
{
    nvs_handle_t nh;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &nh);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_blob(nh, KEY, h, sizeof(hosts_t));
    if (err == ESP_OK) {
        err = nvs_commit(nh);
    }
    nvs_close(nh);
    return err;
}

esp_err_t hosts_nvs_clear(void)
{
    nvs_handle_t nh;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &nh);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_erase_key(nh, KEY);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK;                   /* 本来就是空的，不算失败 */
    }
    if (err == ESP_OK) {
        err = nvs_commit(nh);
    }
    nvs_close(nh);
    ESP_LOGW(TAG, "已抹掉配对记录");
    return err;
}
