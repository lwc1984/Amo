#ifndef GREATEST_H
#define GREATEST_H
#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;

#define CHECK(cond, msg) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, msg); } \
} while (0)

#define CHECK_STR(a, b) do { \
    if (strcmp((a), (b)) == 0) { g_pass++; } \
    else { g_fail++; printf("  FAIL %s:%d  \"%s\" != \"%s\"\n", __FILE__, __LINE__, (a), (b)); } \
} while (0)

#define CHECK_INT(a, b) do { \
    if ((a) == (b)) { g_pass++; } \
    else { g_fail++; printf("  FAIL %s:%d  %d != %d\n", __FILE__, __LINE__, (int)(a), (int)(b)); } \
} while (0)

#define REPORT() do { \
    printf("%d passed, %d failed\n", g_pass, g_fail); \
    return g_fail ? 1 : 0; \
} while (0)

#endif
