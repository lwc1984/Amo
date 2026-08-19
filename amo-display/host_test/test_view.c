#include "greatest.h"
#include "../main/poller.h"
#include <stdio.h>

static void mk_host(hosts_t *h, const char *id, const char *name)
{
    host_t x = {0};
    snprintf(x.host_id, sizeof(x.host_id), "%s", id);
    snprintf(x.name, sizeof(x.name), "%s", name);
    snprintf(x.ip, sizeof(x.ip), "1.2.3.4");
    x.port = 8787;
    hosts_upsert(h, &x);
}

static void test_no_hosts_is_nolink(void) {
    hosts_t h; hosts_init(&h);
    view_t v;
    view_build(&h, NULL, NULL, &v);
    CHECK_INT(v.state, VS_NOLINK);
}

static void test_all_unreachable_is_nolink(void) {
    hosts_t h; hosts_init(&h);
    mk_host(&h, "id0", "A");
    tiny_t s[1] = {0};
    bool ok[1] = { false };
    view_t v;
    view_build(&h, s, ok, &v);
    CHECK_INT(v.state, VS_NOLINK);
}

static void test_waiting_wins_on_current_host(void) {
    hosts_t h; hosts_init(&h);
    mk_host(&h, "id0", "A");
    tiny_t s[1] = {0};
    s[0].waiting = 1; s[0].running = 2;
    snprintf(s[0].name, sizeof(s[0].name), "恐龙公园初始化");
    snprintf(s[0].detail, sizeof(s[0].detail), "Bash");
    bool ok[1] = { true };
    view_t v;
    view_build(&h, s, ok, &v);
    CHECK_INT(v.state, VS_WAITING);
    CHECK_STR(v.name, "恐龙公园初始化");
    CHECK_STR(v.detail, "Bash");
}

static void test_stale_outranks_running(void) {
    /* 与 tray.py:overall_state 的优先级一致：等待 > 失联 > 运行 > 空闲。
       会话失联说明出了问题，比一个正常跑着的更该被看见。
       两块屏对同一台机器必须给出相同结论，否则「颜色只编码状态」就破了。 */
    hosts_t h; hosts_init(&h);
    mk_host(&h, "id0", "A");
    tiny_t s[1] = {0};
    s[0].running = 2;
    s[0].stale = 1;
    bool ok[1] = { true };
    view_t v;
    view_build(&h, s, ok, &v);
    CHECK_INT(v.state, VS_STALE);
}

static void test_waiting_outranks_stale(void) {
    hosts_t h; hosts_init(&h);
    mk_host(&h, "id0", "A");
    tiny_t s[1] = {0};
    s[0].waiting = 1;
    s[0].stale = 1;
    bool ok[1] = { true };
    view_t v;
    view_build(&h, s, ok, &v);
    CHECK_INT(v.state, VS_WAITING);
}

static void test_busy_outranks_running(void) {
    /* 与 tray.py:overall_state、sessions._ORDER 同序：
       等待 > 失联 > 憋大招 > 运行 > 摸鱼。
       憋了十分钟大招比刚敲下回车信息量大，该先看见。 */
    hosts_t h; hosts_init(&h);
    mk_host(&h, "id0", "A");
    tiny_t s[1] = {0};
    s[0].running = 2;
    s[0].busy = 1;
    bool ok[1] = { true };
    view_t v;
    view_build(&h, s, ok, &v);
    CHECK_INT(v.state, VS_BUSY);
}

static void test_stale_outranks_busy(void) {
    hosts_t h; hosts_init(&h);
    mk_host(&h, "id0", "A");
    tiny_t s[1] = {0};
    s[0].busy = 1;
    s[0].stale = 1;
    bool ok[1] = { true };
    view_t v;
    view_build(&h, s, ok, &v);
    CHECK_INT(v.state, VS_STALE);
}

static void test_total_sessions_is_the_sum(void) {
    /* 板子只显示一条会话，但得让人知道背后还有几条 —— 否则五个会话里
       只看见最要紧那条，会以为就这一个。 */
    hosts_t h; hosts_init(&h);
    mk_host(&h, "id0", "A");
    tiny_t s[1] = {0};
    s[0].waiting = 1; s[0].running = 2; s[0].idle = 3;
    s[0].stale = 1;   s[0].busy = 1;
    bool ok[1] = { true };
    view_t v;
    view_build(&h, s, ok, &v);
    CHECK_INT(v.total, 8);
}

static void test_running_when_no_waiting(void) {
    hosts_t h; hosts_init(&h);
    mk_host(&h, "id0", "A");
    tiny_t s[1] = {0};
    s[0].running = 1;
    bool ok[1] = { true };
    view_t v;
    view_build(&h, s, ok, &v);
    CHECK_INT(v.state, VS_RUNNING);
}

static void test_idle_shows_count(void) {
    hosts_t h; hosts_init(&h);
    mk_host(&h, "id0", "A");
    tiny_t s[1] = {0};
    s[0].idle = 3;
    bool ok[1] = { true };
    view_t v;
    view_build(&h, s, ok, &v);
    CHECK_INT(v.state, VS_IDLE);
    CHECK_INT(v.idle_count, 3);
}

static void test_peek_flags_other_host_waiting(void) {
    /* 盯着 A，B 有人在等 —— 右边缘要出标记，否则切错机器就漏了提醒 */
    hosts_t h; hosts_init(&h);
    mk_host(&h, "id0", "A");
    mk_host(&h, "id1", "B");
    tiny_t s[2] = {0};
    s[0].idle = 1;
    s[1].waiting = 1;
    bool ok[2] = { true, true };
    view_t v;
    view_build(&h, s, ok, &v);
    CHECK_INT(v.state, VS_IDLE);
    CHECK(v.peer_needs_you, "另一台在等，应出偷看标记");
    CHECK_STR(v.peer_name, "B");
}

static void test_no_peek_when_current_host_is_the_waiting_one(void) {
    hosts_t h; hosts_init(&h);
    mk_host(&h, "id0", "A");
    mk_host(&h, "id1", "B");
    tiny_t s[2] = {0};
    s[0].waiting = 1;
    s[1].idle = 1;
    bool ok[2] = { true, true };
    view_t v;
    view_build(&h, s, ok, &v);
    CHECK_INT(v.state, VS_WAITING);
    CHECK(!v.peer_needs_you, "自己就在等，不该再出别人的标记");
}

static void test_no_peek_from_unreachable_peer(void) {
    /* 够不着的那台，上一次采样是什么都不算数 —— 不能拿过期数据喊人。 */
    hosts_t h; hosts_init(&h);
    mk_host(&h, "id0", "A");
    mk_host(&h, "id1", "B");
    tiny_t s[2] = {0};
    s[0].idle = 1;
    s[1].waiting = 1;
    bool ok[2] = { true, false };
    view_t v;
    view_build(&h, s, ok, &v);
    CHECK(!v.peer_needs_you, "够不着的主机不该产生偷看标记");
}

static void test_metrics_come_from_current_host(void) {
    hosts_t h; hosts_init(&h);
    mk_host(&h, "id0", "A");
    mk_host(&h, "id1", "B");
    tiny_t s[2] = {0};
    s[0].cpu = 11; s[0].mem = 22; s[0].gpu = -1; s[0].net_kb = 44;
    s[1].cpu = 99;
    bool ok[2] = { true, true };
    view_t v;
    view_build(&h, s, ok, &v);
    CHECK_INT(v.cpu, 11);
    CHECK_INT(v.gpu, -1);
}

int main(void) {
    printf("view:\n");
    test_no_hosts_is_nolink();
    test_all_unreachable_is_nolink();
    test_waiting_wins_on_current_host();
    test_stale_outranks_running();
    test_waiting_outranks_stale();
    test_busy_outranks_running();
    test_stale_outranks_busy();
    test_total_sessions_is_the_sum();
    test_running_when_no_waiting();
    test_idle_shows_count();
    test_peek_flags_other_host_waiting();
    test_no_peek_when_current_host_is_the_waiting_one();
    test_no_peek_from_unreachable_peer();
    test_metrics_come_from_current_host();
    REPORT();
}
