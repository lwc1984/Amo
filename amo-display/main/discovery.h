#pragma once
#include "hosts.h"

/* 查询 _agentdash._tcp，把发现的主机填进 found（只填 host_id/name/ip/port）。
   返回发现数量。

   TXT 里只有 v/host/id，没有令牌 —— 发现和授权是两件事：同事的机器在 mDNS 上
   同样可见，但拿不到令牌就读不到任何会话数据。令牌只能通过配对窗口取得。 */
int discovery_scan(host_t *found, int max, int timeout_ms);
