#pragma once
#include <stdbool.h>

#include "hosts.h"
#include "view.h"
#include "tiny_parse.h"

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
