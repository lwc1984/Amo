#include "discovery.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_netif.h"
#include "mdns.h"

static const char *TAG = "disco";

static const char *txt_get(const mdns_result_t *r, const char *key)
{
    for (size_t i = 0; i < r->txt_count; i++) {
        if (strcmp(r->txt[i].key, key) == 0) {
            return r->txt_value_len[i] ? r->txt[i].value : "";
        }
    }
    return NULL;
}

/* 一条记录可能带多个地址（IPv4 与 IPv6 混在同一个链表里）。
   必须挑出 IPv4 —— 直接取首个会在对端启用 IPv6 时读到错的联合体成员。 */
static bool first_ipv4(const mdns_result_t *r, char *out, size_t cap)
{
    for (mdns_ip_addr_t *a = r->addr; a; a = a->next) {
        if (a->addr.type == ESP_IPADDR_TYPE_V4) {
            snprintf(out, cap, IPSTR, IP2STR(&a->addr.u_addr.ip4));
            return true;
        }
    }
    return false;
}

int discovery_scan(host_t *found, int max, int timeout_ms)
{
    mdns_result_t *results = NULL;
    esp_err_t err = mdns_query_ptr("_agentdash", "_tcp", timeout_ms, 20, &results);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "查询失败: %s", esp_err_to_name(err));
        return 0;
    }

    int n = 0;
    for (mdns_result_t *r = results; r && n < max; r = r->next) {
        const char *id = txt_get(r, "id");
        if (!id || !*id) {
            continue;                     /* 没有 id 的不是我们的服务 */
        }
        char ip[16];
        if (!first_ipv4(r, ip, sizeof(ip))) {
            ESP_LOGW(TAG, "%s 只有 IPv6 地址，跳过", id);
            continue;
        }

        const char *host = txt_get(r, "host");
        host_t *h = &found[n];
        memset(h, 0, sizeof(*h));
        snprintf(h->host_id, sizeof(h->host_id), "%s", id);
        snprintf(h->name, sizeof(h->name), "%s", (host && *host) ? host : id);
        snprintf(h->ip, sizeof(h->ip), "%s", ip);
        h->port = r->port;
        n++;
    }

    mdns_query_results_free(results);
    ESP_LOGI(TAG, "发现 %d 台", n);
    return n;
}
