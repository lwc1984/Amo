#include "pairing.h"

#include <stdio.h>
#include <string.h>

#include "esp_http_client.h"
#include "esp_log.h"

static const char *TAG = "pair";

/* 从 {"token":"xxx",...} 里抠出 token。不引 cJSON —— 只有一个字段要读，
   为此背一个 JSON 解析器进固件不值得。 */
static bool extract_token(const char *json, char *out, size_t cap)
{
    const char *k = strstr(json, "\"token\"");
    if (!k) {
        return false;
    }
    const char *q = strchr(k + 7, '"');
    if (!q) {
        return false;
    }
    q++;
    const char *end = strchr(q, '"');
    if (!end || (size_t)(end - q) >= cap) {
        return false;
    }
    memcpy(out, q, (size_t)(end - q));
    out[end - q] = '\0';
    return true;
}

bool pairing_request(const char *ip, int port, char *token_out, size_t cap)
{
    char url[64];
    snprintf(url, sizeof(url), "http://%s:%d/api/pair", ip, port);

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 3000,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) {
        return false;
    }

    bool ok = false;
    if (esp_http_client_open(c, 0) == ESP_OK) {
        esp_http_client_fetch_headers(c);
        int status = esp_http_client_get_status_code(c);
        if (status == 200) {
            char buf[256] = {0};
            int n = esp_http_client_read_response(c, buf, sizeof(buf) - 1);
            if (n > 0 && extract_token(buf, token_out, cap)) {
                ok = true;
            } else {
                ESP_LOGW(TAG, "200 但没读到 token");
            }
        } else {
            ESP_LOGW(TAG, "%s 返回 %d（配对窗口没开？）", url, status);
        }
    } else {
        ESP_LOGW(TAG, "连不上 %s", url);
    }
    esp_http_client_cleanup(c);
    return ok;
}
