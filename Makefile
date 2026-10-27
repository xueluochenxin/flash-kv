CC       ?= gcc
CFLAGS   ?= -std=gnu11 -g -O0 -Wall -Wextra
CPPFLAGS += -I.
LDLIBS   += -luring

# 网络层二选一，必须和 flash_kvstore.c 里的 NET_ENGINE 保持一致：
#   NET_ENGINE = REACTOR  ->  NET_SRC := reactor.c
#   NET_ENGINE = PROACTOR ->  NET_SRC := proactor.c
# 两个不能同时进 SRCS：它们都定义了 g_protocol / init_server / set_nonblocking，
# 一起编译会 "multiple definition of ..." 链接失败。
NET_SRC := proactor.c

# 四个存储引擎全部参与编译，靠 STORE_ENGINE 决定实际用哪个。
# proactor_conn.c 是旧草稿，自己定义了 proactor_start，不要加进来。
SRCS := flash_kvstore.c $(NET_SRC) array.c RBTREE.c hashmap.c skiplist.c kvmemctl.c
HDRS := $(wildcard *.h)

TARGET := flash_kv

all: $(TARGET)

$(TARGET): $(SRCS) $(HDRS)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SRCS) -o $@ $(LDLIBS)

clean:
	rm -f $(TARGET)

.PHONY: all clean
