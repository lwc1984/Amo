#pragma once
#include <stdbool.h>

#define TINY_VERSION 1

typedef struct {
    int  waiting, running, idle;
    char host[32];
    char name[64];
    char detail[80];
    int  cpu, mem, gpu, net_kb;
} tiny_t;

/* 解析 /api/tiny 的三行响应。失败返回 false，此时 *out 内容未定义。
   不信任对端：任何字段超长都截断，不溢出。 */
bool tiny_parse(const char *body, tiny_t *out);
