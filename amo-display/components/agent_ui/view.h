#pragma once
#include <stdbool.h>

/* 显示模型。放在 agent_ui 而不是 main，是为了让依赖方向单向：
   main（产生视图）-> agent_ui（消费视图）。反过来会形成组件循环，
   IDF 会直接报错，而且那也说明职责划错了。 */

/* 六种显示态。
   VS_NOLINK 与 VS_STALE 是两回事，不得合并：前者是「板子够不着任何主机」，
   后者是「主机说某个会话失联了」。合并的话，网线松了和 Claude 卡住会长成
   一个样子。
   VS_BUSY 与 VS_RUNNING 颜色相同但状态不同：都不需要你走过去，所以 2 米外
   该长得一样；走近了要能分出「刚开始」和「已经憋了十分钟」。 */
typedef enum {
    VS_NOLINK, VS_IDLE, VS_RUNNING, VS_BUSY, VS_WAITING, VS_STALE
} view_state_t;

typedef struct {
    view_state_t state;
    char host[32];
    char name[64];
    char detail[80];
    int  idle_count;
    int  total;                 /* 该主机上的会话总数。板子只显示一条，
                                   但得让人知道背后还有几条。 */
    int  cpu, mem, gpu, net_kb;
    bool peer_needs_you;        /* 另一台已配对主机有人在等 */
    char peer_name[32];
} view_t;
