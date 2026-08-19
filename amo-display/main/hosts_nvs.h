#pragma once
#include "esp_err.h"
#include "hosts.h"

/* 主机表的 NVS 持久化 —— 整个固件里唯一碰 ESP 存储 API 的地方，
   hosts.c 保持纯逻辑以便在主机上单测。 */
esp_err_t hosts_nvs_load(hosts_t *h);
esp_err_t hosts_nvs_save(const hosts_t *h);

/* 抹掉已配对主机。不可撤销 —— 清掉之后必须重新走一遍宿主端的配对窗口
   才能再读到任何数据。找不到键时返回 ESP_OK（本来就是空的）。 */
esp_err_t hosts_nvs_clear(void);
