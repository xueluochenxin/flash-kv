CC       ?= gcc
CFLAGS   ?= -std=gnu11 -g -O0 -Wall -Wextra
CPPFLAGS += -I.

# ── 网络层三选一 ────────────────────────────────────────────────
# 必须和 flash_kvstore.c 第 11 行的 NET_ENGINE 保持一致：
#   NTYCO    ->  coroutinue.c  +  4 个 ntyco 源文件
#   REACTOR  ->  reactor.c
#   PROACTOR ->  proactor.c
# 三者不能同时进 SRCS：reactor.c / proactor.c 都定义了 g_protocol / init_server /
# set_nonblocking，ntyco 还额外定义了 socket/recv/send/accept/close 这些同名符号，
# 一起链接不是重复定义就是全局劫持。
NET_SRC   := coroutinue.c
NTYCO_SRC := nty_coroutine.c nty_schedule.c nty_epoll.c nty_socket.c

# 存储层：四个引擎全部参与编译，由 STORE_ENGINE 决定实际用哪个
STORE_SRC := array.c RBTREE.c hashmap.c skiplist.c kvmemctl.c

SRCS := flash_kvstore.c $(NET_SRC) $(NTYCO_SRC) $(STORE_SRC)
HDRS := $(wildcard *.h)

TARGET := flash_kv

# ntyco 需要 pthread（TLS / pthread_once）和 dl（dlsym 取 libc 真函数）
LDLIBS += -lpthread -ldl

all: $(TARGET)

$(TARGET): $(SRCS) $(HDRS)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SRCS) -o $@ $(LDLIBS)

clean:
	rm -f $(TARGET)

.PHONY: all clean
