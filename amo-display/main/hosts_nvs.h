#pragma once
#include "esp_err.h"
#include "hosts.h"

/* 主机表的 NVS 持久化 —— 整个固件里唯一碰 ESP 存储 API 的地方，
   hosts.c 保持纯逻辑以便在主机上单测。 */
esp_err_t hosts_nvs_load(hosts_t *h);
esp_err_t hosts_nvs_save(const hosts_t *h);
