CC       ?= gcc
CFLAGS   ?= -std=gnu11 -g -O0 -Wall -Wextra
CPPFLAGS += -I.

# 当前 flash_kvstore.c 里 STORE_ENGINE = SKIPLIST_S。
# skiplist.c 已加入构建，kvs_skiplist_protocol 已经在 skiplist.c 中实现。
SRCS := flash_kvstore.c reactor.c array.c RBTREE.c hashmap.c skiplist.c kvmemctl.c
HDRS := reactor.h array.h RBTREE.h hashmap.h skiplist.h kvmemctl.h

TARGET := flash_kv

all: $(TARGET)

$(TARGET): $(SRCS) $(HDRS)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SRCS) -o $@

clean:
	rm -f $(TARGET)