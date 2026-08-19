#pragma once
#include <stdbool.h>

/* 显示模型。放在 agent_ui 而不是 main，是为了让依赖方向单向：
   main（产生视图）-> agent_ui（消费视图）。反过来会形成组件循环，
   IDF 会直接报错，而且那也说明职责划错了。 */

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
