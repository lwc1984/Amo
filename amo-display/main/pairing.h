#pragma once
#include <stdbool.h>
#include <stddef.h>

/* 向指定主机的 /api/pair 要令牌。只有宿主在托盘点过「配对新设备」、窗口还开着时
   才会返回 200，否则 403。成功时把令牌写进 token_out。 */
bool pairing_request(const char *ip, int port, char *token_out, size_t cap);
