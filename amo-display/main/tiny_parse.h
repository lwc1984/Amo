#pragma once
#include <stdbool.h>

#define TINY_VERSION 3

typedef struct {
    int  waiting, running, idle, stale, busy;
    char host[32];
    char name[64];
    char detail[80];
    int  cpu, mem, gpu, net_kb;
} tiny_t;

/* 第一行格式：`版本|waiting,running,idle,stale,busy|主机名`

   五个状态各有自己的计数位。stale 少了这一位，失联会被渲染成「摸鱼中」——
   设计里「断连绝不能渲染成正常」靠的就是它。busy 少了这一位，一次十分钟的
   构建和刚敲下回车长得一样。

   见到不认识的版本整体拒绝而不是猜：宁可显示「连不上」，也不要显示一个错的
   状态。v1 三个计数、v2 四个，都会被现在的固件拒掉。

   解析 /api/tiny 的三行响应。失败返回 false，此时 *out 内容未定义。
   不信任对端：任何字段超长都截断，不溢出。 */
bool tiny_parse(const char *body, tiny_t *out);
