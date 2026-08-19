# make 内建就把 CC 定义成 cc 了，所以 `CC ?= gcc` 是无效的 —— 只有当 CC 仍是
# 内建默认值时才改成 gcc，命令行/环境变量传进来的 CC 依然优先。
ifeq ($(origin CC),default)
CC = gcc
endif
CFLAGS = -std=c11 -Wall -Wextra -Werror -g

all: test_tiny_parse
	./test_tiny_parse

test_tiny_parse: test_tiny_parse.c ../main/tiny_parse.c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f test_tiny_parse test_hosts *.exe
