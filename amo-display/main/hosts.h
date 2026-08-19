#pragma once
#include <stdbool.h>

#define HOSTS_MAX 4

typedef struct {
    char host_id[16];      /* 宿主端 uuid4().hex[:8]，IP 变了也认得出是同一台 */
    char name[32];
    char ip[16];
    int  port;
    char token[40];        /* 32 位十六进制 + 余量 */
    bool online;
} host_t;

typedef struct {
    host_t items[HOSTS_MAX];
    int    count;
    int    current;        /* 当前盯着哪一台 */
} hosts_t;

void hosts_init(hosts_t *h);
int  hosts_find(const hosts_t *h, const char *host_id);
bool hosts_upsert(hosts_t *h, const host_t *item);
void hosts_set_addr(hosts_t *h, const char *host_id, const char *ip, int port);
void hosts_cycle(hosts_t *h);
const host_t *hosts_current(const hosts_t *h);
