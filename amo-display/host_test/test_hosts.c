#include "greatest.h"
#include "../main/hosts.h"

static host_t mk(const char *id, const char *name, const char *ip)
{
    host_t h = {0};
    snprintf(h.host_id, sizeof(h.host_id), "%s", id);
    snprintf(h.name, sizeof(h.name), "%s", name);
    snprintf(h.ip, sizeof(h.ip), "%s", ip);
    snprintf(h.token, sizeof(h.token), "tok-%s", id);
    h.port = 8787;
    return h;
}

static void test_starts_empty(void) {
    hosts_t h; hosts_init(&h);
    CHECK_INT(h.count, 0);
    CHECK(hosts_current(&h) == NULL, "空表 current 应为 NULL");
}

static void test_upsert_adds(void) {
    hosts_t h; hosts_init(&h);
    host_t a = mk("a1b2c3d4", "WORKSTATION", "192.168.1.100");
    CHECK(hosts_upsert(&h, &a), "首次插入应成功");
    CHECK_INT(h.count, 1);
    CHECK_STR(hosts_current(&h)->name, "WORKSTATION");
}

static void test_upsert_dedupes_by_host_id(void) {
    hosts_t h; hosts_init(&h);
    host_t a = mk("a1b2c3d4", "WORKSTATION", "192.168.1.100");
    hosts_upsert(&h, &a);
    host_t again = mk("a1b2c3d4", "WORKSTATION", "192.168.1.177");   /* IP 变了 */
    CHECK(hosts_upsert(&h, &again), "重复 host_id 应更新而不是拒绝");
    CHECK_INT(h.count, 1);
    CHECK_STR(h.items[0].ip, "192.168.1.177");
}

static void test_set_addr_only_touches_address(void) {
    hosts_t h; hosts_init(&h);
    host_t a = mk("a1b2c3d4", "WORKSTATION", "192.168.1.100");
    hosts_upsert(&h, &a);
    hosts_set_addr(&h, "a1b2c3d4", "10.0.0.9", 9999);
    CHECK_STR(h.items[0].ip, "10.0.0.9");
    CHECK_INT(h.items[0].port, 9999);
    CHECK_STR(h.items[0].token, "tok-a1b2c3d4");   /* 令牌不能被地址更新冲掉 */
}

static void test_set_addr_ignores_unknown_host(void) {
    hosts_t h; hosts_init(&h);
    hosts_set_addr(&h, "nobody", "10.0.0.9", 1);   /* 不得崩溃 */
    CHECK_INT(h.count, 0);
}

static void test_full_table_rejects(void) {
    hosts_t h; hosts_init(&h);
    for (int i = 0; i < HOSTS_MAX; i++) {
        char id[16];
        snprintf(id, sizeof(id), "id%d", i);
        host_t x = mk(id, "H", "1.2.3.4");
        CHECK(hosts_upsert(&h, &x), "未满时应成功");
    }
    host_t extra = mk("overflow", "H", "1.2.3.4");
    CHECK(!hosts_upsert(&h, &extra), "满了应返回 false 而不是覆盖");
    CHECK_INT(h.count, HOSTS_MAX);
}

static void test_cycle_wraps(void) {
    hosts_t h; hosts_init(&h);
    host_t a = mk("id0", "A", "1.1.1.1");
    host_t b = mk("id1", "B", "2.2.2.2");
    hosts_upsert(&h, &a);
    hosts_upsert(&h, &b);
    CHECK_STR(hosts_current(&h)->name, "A");
    hosts_cycle(&h);
    CHECK_STR(hosts_current(&h)->name, "B");
    hosts_cycle(&h);
    CHECK_STR(hosts_current(&h)->name, "A");
}

static void test_cycle_on_empty_does_not_crash(void) {
    hosts_t h; hosts_init(&h);
    hosts_cycle(&h);
    CHECK(hosts_current(&h) == NULL, "空表 cycle 后仍应为 NULL");
}

int main(void) {
    printf("hosts:\n");
    test_starts_empty();
    test_upsert_adds();
    test_upsert_dedupes_by_host_id();
    test_set_addr_only_touches_address();
    test_set_addr_ignores_unknown_host();
    test_full_table_rejects();
    test_cycle_wraps();
    test_cycle_on_empty_does_not_crash();
    REPORT();
}
