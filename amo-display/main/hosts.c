#include "hosts.h"
#include <string.h>
#include <stdio.h>

void hosts_init(hosts_t *h)
{
    memset(h, 0, sizeof(*h));
}

int hosts_find(const hosts_t *h, const char *host_id)
{
    for (int i = 0; i < h->count; i++) {
        if (strcmp(h->items[i].host_id, host_id) == 0) {
            return i;
        }
    }
    return -1;
}

bool hosts_upsert(hosts_t *h, const host_t *item)
{
    int at = hosts_find(h, item->host_id);
    if (at >= 0) {
        h->items[at] = *item;          /* 同一台，整条更新 */
        return true;
    }
    if (h->count >= HOSTS_MAX) {
        return false;                  /* 满了就拒绝，不静默覆盖别人 */
    }
    h->items[h->count++] = *item;
    return true;
}

void hosts_set_addr(hosts_t *h, const char *host_id, const char *ip, int port)
{
    int at = hosts_find(h, host_id);
    if (at < 0) {
        return;                        /* 没配对过的主机，地址不记 */
    }
    snprintf(h->items[at].ip, sizeof(h->items[at].ip), "%s", ip);
    h->items[at].port = port;
}

void hosts_cycle(hosts_t *h)
{
    if (h->count <= 0) {
        return;
    }
    h->current = (h->current + 1) % h->count;
}

const host_t *hosts_current(const hosts_t *h)
{
    if (h->count <= 0) {
        return NULL;
    }
    return &h->items[h->current];
}
