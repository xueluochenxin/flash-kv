CC       ?= gcc
CFLAGS   ?= -std=gnu11 -g -O0 -Wall -Wextra
CPPFLAGS += -I.

# ════════════════════════════════════════════════════════════════
#  用法：
#     make                          # 默认：ntyco + skiplist
#     make NET=reactor              # 换网络层
#     make NET=proactor             # 换网络层（需要 liburing）
#     make STORE=HASH_S             # 换存储引擎
#     make NET=proactor STORE=ARRAY_S
#
#  注意：改 NET / STORE 之后一定要 make clean，因为目标文件是同一个。
# ════════════════════════════════════════════════════════════════

# ── 网络层三选一 ────────────────────────────────────────────────
# 三者不能同时进 SRCS：
#   reactor.c / proactor.c 都定义了 g_protocol / init_server / set_nonblocking；
#   ntyco 还额外 hook 了 socket/recv/send/accept/close 这些 libc 符号。
#   一起链接不是重复定义，就是全局劫持。
NET ?= ntyco

ifeq ($(NET),ntyco)
  NET_SRC    := coroutinue.c
  NTYCO_SRC  := nty_coroutine.c nty_schedule.c nty_epoll.c nty_socket.c
  NET_LIBS   := -lpthread -ldl
  NET_ENGINE := NTYCO
else ifeq ($(NET),reactor)
  NET_SRC    := reactor.c
  NTYCO_SRC  :=
  NET_LIBS   := -lpthread -ldl
  NET_ENGINE := REACTOR
else ifeq ($(NET),proactor)
  NET_SRC    := proactor.c
  NTYCO_SRC  :=
  NET_LIBS   := -lpthread -ldl -luring
  NET_ENGINE := PROACTOR
else
  $(error NET 只能是 ntyco / reactor / proactor，当前是 "$(NET)")
endif

# ── 存储引擎四选一 ──────────────────────────────────────────────
# 四个引擎全部参与编译，由 STORE_ENGINE 决定实际用哪个
STORE ?= SKIPLIST_S
STORE_VALID := ARRAY_S RBTREE_S HASH_S SKIPLIST_S

ifeq ($(filter $(STORE),$(STORE_VALID)),)
  $(error STORE 只能是 $(STORE_VALID)，当前是 "$(STORE)")
endif

STORE_SRC := array.c RBTREE.c hashmap.c skiplist.c kvmemctl.c kvbuf.c

# 把选择结果传给 flash_kvstore.c
CPPFLAGS += -DNET_ENGINE=$(NET_ENGINE) -DSTORE_ENGINE=$(STORE)

SRCS := flash_kvstore.c $(NET_SRC) $(NTYCO_SRC) $(STORE_SRC)
HDRS := $(wildcard *.h)

TARGET := flash_kv

LDLIBS += $(NET_LIBS)

all: $(TARGET)

$(TARGET): $(SRCS) $(HDRS)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SRCS) -o $@ $(LDLIBS)
	@echo "== 已构建 $@  网络层=$(NET)  存储层=$(STORE) =="

# ── kvbuf 的独立单测：只编 kvbuf.c + test_kvbuf.c，不碰服务器和网络层 ──
test_kvbuf: kvbuf.c test_kvbuf.c kvbuf.h
	$(CC) $(CFLAGS) $(CPPFLAGS) kvbuf.c test_kvbuf.c -o $@

# ── 测试客户端：复用 kvbuf.c，不依赖任何网络层 ──
test_client: test_client.c kvbuf.c kvbuf.h
	$(CC) $(CFLAGS) $(CPPFLAGS) test_client.c kvbuf.c -o $@

clean:
	rm -f $(TARGET) test_kvbuf test_client

.PHONY: all clean test_kvbuf test_client
