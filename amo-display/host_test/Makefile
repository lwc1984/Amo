# make 内建就把 CC 定义成 cc 了，所以 `CC ?= gcc` 是无效的 —— 只有当 CC 仍是
# 内建默认值时才改成 gcc，命令行/环境变量传进来的 CC 依然优先。
ifeq ($(origin CC),default)
CC = gcc
endif
CFLAGS = -std=c11 -Wall -Wextra -Werror -g

all: test_tiny_parse test_hosts test_view
	./test_tiny_parse
	./test_hosts
	./test_view

test_tiny_parse: test_tiny_parse.c ../main/tiny_parse.c
	$(CC) $(CFLAGS) -o $@ $^

test_hosts: test_hosts.c ../main/hosts.c
	$(CC) $(CFLAGS) -o $@ $^

# POLLER_HOST_TEST 切掉 poller.c 里依赖 FreeRTOS 与 esp_http_client 的部分，
# 只留纯函数 view_build —— 状态判定与偷看标记的逻辑因此能在主机上测。
test_view: test_view.c ../main/poller.c ../main/hosts.c
	$(CC) $(CFLAGS) -DPOLLER_HOST_TEST -o $@ $^

clean:
	rm -f test_tiny_parse test_hosts test_view *.exe
