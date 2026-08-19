#pragma once
#include <stdbool.h>

#define TINY_VERSION 2

typedef struct {
    int  waiting, running, idle, stale;
    char host[32];
    char name[64];
    char detail[80];
    int  cpu, mem, gpu, net_kb;
} tiny_t;

/* 第一行格式：`版本|waiting,running,idle,stale|主机名`

   stale 必须有自己的计数位。它不属于前三者中的任何一个，少了这一位，
   一个会话失联时固件只看到 0,0,0，会把「没声儿了」渲染成「摸鱼中」——
   设计里「断连绝不能渲染成正常」靠的就是这一位。

   v1 只有三个计数，见到不认识的版本整体拒绝而不是猜：宁可显示「连不上」，
   也不要显示一个错的状态。

   解析 /api/tiny 的三行响应。失败返回 false，此时 *out 内容未定义。
   不信任对端：任何字段超长都截断，不溢出。 */
bool tiny_parse(const char *body, tiny_t *out);
