#pragma once
#include "hosts.h"

/* 单次 mDNS 查询会随机落空（组播 UDP 丢包 / 应答晚于超时），
   所以内部重试这么多次，任一次查到就返回。 */
#define DISCOVERY_ATTEMPTS 3

/* 查询 _agentdash._tcp，把发现的主机填进 found（只填 host_id/name/ip/port）。
   返回发现数量。

   TXT 里只有 v/host/id，没有令牌 —— 发现和授权是两件事：同事的机器在 mDNS 上
   同样可见，但拿不到令牌就读不到任何会话数据。令牌只能通过配对窗口取得。 */
int discovery_scan(host_t *found, int max, int timeout_ms);
