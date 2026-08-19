#include "tiny_parse.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* 从 src 拷贝到 dst，最多 cap-1 字节，保证 NUL 结尾。 */
static void copy_clamped(char *dst, size_t cap, const char *src, size_t len)
{
    if (len >= cap) {
        len = cap - 1;
    }
    memcpy(dst, src, len);
    dst[len] = '\0';
}

/* 返回本行结束位置（指向 '\n' 或字符串结尾）。 */
static const char *line_end(const char *p)
{
    while (*p && *p != '\n') {
        p++;
    }
    return p;
}

bool tiny_parse(const char *body, tiny_t *out)
{
    if (!body || !out) {
        return false;
    }

    /* ---- 第 1 行：版本|w,r,i|主机名 ---- */
    const char *l1 = body;
    const char *l1_end = line_end(l1);
    if (*l1_end != '\n') {
        return false;                       /* 只有一行，不合法 */
    }

    int ver = 0, w = 0, r = 0, i = 0;
    int consumed = 0;
    if (sscanf(l1, "%d|%d,%d,%d|%n", &ver, &w, &r, &i, &consumed) != 4 || consumed == 0) {
        return false;
    }
    if (ver != TINY_VERSION) {
        return false;                       /* 版本不认识就整体拒绝，别猜 */
    }
    const char *host = l1 + consumed;
    if (host > l1_end) {
        return false;
    }
    copy_clamped(out->host, sizeof(out->host), host, (size_t)(l1_end - host));
    out->waiting = w;
    out->running = r;
    out->idle = i;

    /* ---- 第 2 行：会话名\tdetail（可为空行）---- */
    const char *l2 = l1_end + 1;
    const char *l2_end = line_end(l2);
    if (*l2_end != '\n') {
        return false;                       /* 缺第三行 */
    }
    const char *tab = memchr(l2, '\t', (size_t)(l2_end - l2));
    if (tab) {
        copy_clamped(out->name, sizeof(out->name), l2, (size_t)(tab - l2));
        copy_clamped(out->detail, sizeof(out->detail), tab + 1, (size_t)(l2_end - tab - 1));
    } else {
        copy_clamped(out->name, sizeof(out->name), l2, (size_t)(l2_end - l2));
        out->detail[0] = '\0';
    }

    /* ---- 第 3 行：cpu,mem,gpu,net ---- */
    const char *l3 = l2_end + 1;
    if (sscanf(l3, "%d,%d,%d,%d", &out->cpu, &out->mem, &out->gpu, &out->net_kb) != 4) {
        return false;
    }

    return true;
}
