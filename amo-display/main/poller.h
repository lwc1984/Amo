#pragma once
#include <stdbool.h>

#include "hosts.h"
#include "tiny_parse.h"

/* 五种显示态。VS_NOLINK 与 VS_STALE 是两回事，不得合并：
   前者是「板子够不着任何主机」，后者是「主机说某个会话失联了」。
   合并的话，网线松了和 Claude 卡住会长成一个样子。 */
typedef enum { VS_NOLINK, VS_IDLE, VS_RUNNING, VS_WAITING, VS_STALE } view_state_t;

typedef struct {
    view_state_t state;
    char host[32];
    char name[64];
    char detail[80];
    int  idle_count;
    int  cpu, mem, gpu, net_kb;
    bool peer_needs_you;        /* 另一台已配对主机有人在等 */
    char peer_name[32];
} view_t;

/* 纯函数：由所有主机的采样合成当前该显示什么。
   samples/ok 的下标与 h->items 一一对应；h->count 为 0 时两者可为 NULL。

   状态优先级与 tray.py:overall_state 一致：等待 > 失联 > 运行 > 空闲。
   两块屏对同一台机器必须给出相同结论。 */
void view_build(const hosts_t *h, const tiny_t *samples, const bool *ok, view_t *out);

#ifndef POLLER_HOST_TEST
/* 起后台轮询任务。h 必须在整个程序生命周期内有效。 */
void poller_start(hosts_t *h);

/* 取最新视图。拿不到锁时返回 false，调用方沿用上一帧即可。 */
bool poller_take(view_t *out);
#endif
