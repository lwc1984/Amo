#include "poller.h"

#include <stdio.h>
#include <string.h>

void view_build(const hosts_t *h, const tiny_t *samples, const bool *ok, view_t *out)
{
    memset(out, 0, sizeof(*out));
    out->gpu = -1;

    if (!h || h->count <= 0 || !samples || !ok) {
        out->state = VS_NOLINK;
        return;
    }

    int cur = h->current;
    if (cur < 0 || cur >= h->count || !ok[cur]) {
        /* 当前这台够不着就是够不着，别假装正常。
           设计里那条「断连绝不能渲染成正常」就是这一行。 */
        out->state = VS_NOLINK;
        return;
    }

    const tiny_t *s = &samples[cur];
    snprintf(out->host, sizeof(out->host), "%s", h->items[cur].name);
    snprintf(out->name, sizeof(out->name), "%s", s->name);
    snprintf(out->detail, sizeof(out->detail), "%s", s->detail);
    out->idle_count = s->idle;
    out->total = s->waiting + s->running + s->idle + s->stale + s->busy;
    out->cpu = s->cpu;
    out->mem = s->mem;
    out->gpu = s->gpu;
    out->net_kb = s->net_kb;

    /* 优先级与 tray.py:overall_state、sessions._ORDER 同序：
       等待 > 失联 > 憋大招 > 运行 > 摸鱼。
       stale 排在 running 之前是有意的 —— 失联说明出了问题，比正常跑着的更该
       被看见。busy 排在 running 之前同理：憋了十分钟大招比刚敲下回车信息量大。 */
    if (s->waiting > 0) {
        out->state = VS_WAITING;
    } else if (s->stale > 0) {
        out->state = VS_STALE;
    } else if (s->busy > 0) {
        out->state = VS_BUSY;
    } else if (s->running > 0) {
        out->state = VS_RUNNING;
    } else {
        out->state = VS_IDLE;
    }

    /* 偷看：盯着这台的时候，别台有人在等也要能看见 —— 否则切错机器就漏提醒。
       自己已经在 waiting 就不必再标别人；够不着的那台不算数，
       不能拿过期采样喊人。 */
    if (out->state != VS_WAITING) {
        for (int i = 0; i < h->count; i++) {
            if (i == cur || !ok[i]) {
                continue;
            }
            if (samples[i].waiting > 0) {
                out->peer_needs_you = true;
                snprintf(out->peer_name, sizeof(out->peer_name), "%s", h->items[i].name);
                break;
            }
        }
    }
}

#ifndef POLLER_HOST_TEST

#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "poll";

static hosts_t *s_hosts;
static view_t s_view;
static SemaphoreHandle_t s_lock;

static bool fetch_tiny(const host_t *h, tiny_t *out)
{
    char url[160];
    snprintf(url, sizeof(url), "http://%s:%d/api/tiny?k=%s%s",
             h->ip, h->port, h->token,
#ifdef CONFIG_AGENT_TINY_FULL_DETAIL
             "&d=full"
#else
             ""
#endif
    );

    esp_http_client_config_t cfg = { .url = url, .timeout_ms = 2000 };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) {
        return false;
    }

    bool ok = false;
    if (esp_http_client_open(c, 0) == ESP_OK) {
        esp_http_client_fetch_headers(c);
        int status = esp_http_client_get_status_code(c);
        if (status == 200) {
            char buf[320] = {0};
            int n = esp_http_client_read_response(c, buf, sizeof(buf) - 1);
            if (n > 0) {
                ok = tiny_parse(buf, out);
            }
        } else if (status == 401) {
            /* 令牌失效 —— 宿主换了配置文件，或者被重装过。
               这值得单独报，否则表现成"连不上"会让人去查网络。 */
            ESP_LOGW(TAG, "%s 返回 401，令牌已失效，需要重新配对", h->name);
        }
    }
    esp_http_client_cleanup(c);
    return ok;
}

static void poll_task(void *arg)
{
    (void)arg;
    tiny_t samples[HOSTS_MAX];
    bool ok[HOSTS_MAX];

    for (;;) {
        for (int i = 0; i < s_hosts->count; i++) {
            ok[i] = fetch_tiny(&s_hosts->items[i], &samples[i]);
            s_hosts->items[i].online = ok[i];
        }
        view_t v;
        view_build(s_hosts, samples, ok, &v);

        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_view = v;
        xSemaphoreGive(s_lock);

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void poller_start(hosts_t *h)
{
    s_hosts = h;
    s_lock = xSemaphoreCreateMutex();
    xTaskCreate(poll_task, "poll", 6144, NULL, 4, NULL);
}

bool poller_take(view_t *out)
{
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(50)) != pdTRUE) {
        return false;
    }
    *out = s_view;
    xSemaphoreGive(s_lock);
    return true;
}

#endif /* POLLER_HOST_TEST */
