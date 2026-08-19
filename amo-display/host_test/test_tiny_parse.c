#include "greatest.h"
#include "../main/tiny_parse.h"

static void test_full_payload(void) {
    tiny_t t;
    const char *body = "1|3,1,2|WORKSTATION\n恐龙公园初始化\tBash\n12,41,3,8";
    CHECK(tiny_parse(body, &t), "应解析成功");
    CHECK_INT(t.waiting, 3);
    CHECK_INT(t.running, 1);
    CHECK_INT(t.idle, 2);
    CHECK_STR(t.host, "WORKSTATION");
    CHECK_STR(t.name, "恐龙公园初始化");
    CHECK_STR(t.detail, "Bash");
    CHECK_INT(t.cpu, 12);
    CHECK_INT(t.mem, 41);
    CHECK_INT(t.gpu, 3);
    CHECK_INT(t.net_kb, 8);
}

static void test_no_sessions_second_line_empty(void) {
    tiny_t t;
    const char *body = "1|0,0,0|HOSTA\n\n5,6,-1,0";
    CHECK(tiny_parse(body, &t), "空第二行也应解析成功");
    CHECK_INT(t.waiting, 0);
    CHECK_STR(t.name, "");
    CHECK_STR(t.detail, "");
    CHECK_INT(t.gpu, -1);
}

static void test_absent_gpu_is_minus_one(void) {
    tiny_t t;
    tiny_parse("1|0,0,1|H\nx\ty\n1,2,-1,3", &t);
    CHECK_INT(t.gpu, -1);
}

static void test_name_without_detail(void) {
    tiny_t t;
    CHECK(tiny_parse("1|1,0,0|H\n只有名字\n1,2,3,4", &t), "没有 tab 也要能解析");
    CHECK_STR(t.name, "只有名字");
    CHECK_STR(t.detail, "");
}

static void test_rejects_wrong_version(void) {
    tiny_t t;
    CHECK(!tiny_parse("2|1,0,0|H\nx\ty\n1,2,3,4", &t), "版本号不认识就该拒绝");
}

static void test_rejects_truncated_body(void) {
    tiny_t t;
    CHECK(!tiny_parse("1|1,0,0|H\nx\ty", &t), "少一行就该拒绝");
    CHECK(!tiny_parse("", &t), "空 body 就该拒绝");
    CHECK(!tiny_parse("garbage", &t), "垃圾数据就该拒绝");
}

static void test_long_fields_are_truncated_not_overflowed(void) {
    /* 服务端 detail 上限 60 字符，但固件不能信任对端 */
    char body[1024];
    snprintf(body, sizeof(body), "1|1,0,0|%s\n%s\t%s\n1,2,3,4",
             "H23456789012345678901234567890123456789012345678901234567890",
             "N23456789012345678901234567890123456789012345678901234567890123456789012345678901234567890",
             "D23456789012345678901234567890123456789012345678901234567890123456789012345678901234567890");
    tiny_t t;
    CHECK(tiny_parse(body, &t), "超长字段应截断而不是拒绝");
    CHECK(strlen(t.host) < sizeof(t.host), "host 不得溢出");
    CHECK(strlen(t.name) < sizeof(t.name), "name 不得溢出");
    CHECK(strlen(t.detail) < sizeof(t.detail), "detail 不得溢出");
}

static void test_no_cwd_ever_appears(void) {
    /* 宿主端保证不下发 cwd；这里断言解析器也不会凭空造出路径字段 */
    tiny_t t;
    tiny_parse("1|1,0,0|H\nAmo\tBash\n1,2,3,4", &t);
    CHECK(strstr(t.detail, ":\\") == NULL, "detail 里不该有盘符路径");
}

int main(void) {
    printf("tiny_parse:\n");
    test_full_payload();
    test_no_sessions_second_line_empty();
    test_absent_gpu_is_minus_one();
    test_name_without_detail();
    test_rejects_wrong_version();
    test_rejects_truncated_body();
    test_long_fields_are_truncated_not_overflowed();
    test_no_cwd_ever_appears();
    REPORT();
}
