CC       ?= gcc
CFLAGS   ?= -std=gnu11 -g -O0 -Wall -Wextra
CPPFLAGS += -I.

# 当前 flash_kvstore.c 里 STORE_ENGINE = SKIPLIST_S。
# skiplist.c 已经加入构建；但 kvs_skiplist_protocol 还没实现，
# 所以现在 make 会在链接阶段报 undefined reference to kvs_skiplist_protocol。
# 等 protocol 写好后即可直接编译通过。
SRCS := flash_kvstore.c reactor.c array.c RBTREE.c hashmap.c skiplist.c kvmemctl.c
HDRS := reactor.h array.h RBTREE.h hashmap.h skiplist.h kvmemctl.h

TARGET := flash_kv

all: $(TARGET)

$(TARGET): $(SRCS) $(HDRS)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SRCS) -o $@

clean:
	rm -f $(TARGET)