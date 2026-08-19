#pragma once
#include <stdbool.h>

#include "esp_err.h"

/* 连接 WiFi，阻塞直到拿到 IP 或超时（20 秒）。
   凭据来自 menuconfig 的 Agent Display 菜单，落在 sdkconfig 里，不进仓库。 */
esp_err_t net_wifi_start(void);

bool net_wifi_is_up(void);
