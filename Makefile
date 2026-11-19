CC       ?= gcc
CFLAGS   ?= -std=gnu11 -g -O0 -Wall -Wextra
CPPFLAGS += -I.

# ════════════════════════════════════════════════════════════════
#  用法：
#     make                          # 默认：ntyco + skiplist，产出两个二进制
#     make flash_kv                 # 只编主节点
#     make flash_kv_slave           # 只编从节点
#     make NET=reactor              # 换网络层（从节点只支持 ntyco）
#     make STORE=HASH_S             # 换存储引擎
#     make NET=proactor STORE=ARRAY_S
#
#  注意：改 NET / STORE 之后一定要 make clean，因为目标文件同名。
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
STORE ?= SKIPLIST_S
STORE_VALID := ARRAY_S RBTREE_S HASH_S SKIPLIST_S

ifeq ($(filter $(STORE),$(STORE_VALID)),)
  $(error STORE 只能是 $(STORE_VALID)，当前是 "$(STORE)")
endif

STORE_SRC := array.c RBTREE.c hashmap.c skiplist.c kvmemctl.c kvbuf.c

# ── 应用层模块 ──────────────────────────────────────────────────
# kv_persist.c : AOF / dump / load
# kv_repl.c    : 主从同步（主节点部分三层通用，从节点部分只在 ntyco 下有效）
APP_SRC := kv_persist.c kv_repl.c

# 把选择结果传给所有 .c（必须在这里，因为两个目标都要用）
CPPFLAGS += -DNET_ENGINE=$(NET_ENGINE) -DSTORE_ENGINE=$(STORE)

# 主从共用的一组源码：
#   flash_kvstore.c 提供 protocol / kv_store_foreach / kv_store_clear / kv_store_engine_create
#   （它的 main 会被 KV_SLAVE_BUILD 条件编译控制）
COMMON_SRC := flash_kvstore.c $(APP_SRC) $(NET_SRC) $(NTYCO_SRC) $(STORE_SRC)

HDRS := $(wildcard *.h)

TARGET       := flash_kv
SLAVE_TARGET := flash_kv_slave

LDLIBS += $(NET_LIBS)

all: $(TARGET) $(SLAVE_TARGET)

# ── 主节点：flash_kvstore.c 里的 main 生效 ──
$(TARGET): $(COMMON_SRC) $(HDRS)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(COMMON_SRC) -o $@ $(LDLIBS)
	@echo "== 已构建 $@（主节点）网络层=$(NET) 存储层=$(STORE) =="

# ── 从节点：同一份代码 + -DKV_SLAVE_BUILD ──
#    -D 让 flash_kvstore.c 的 main 不参与编译，
#    入口改由 slave_main.c 提供；其余（protocol / 遍历 / 清库）全部复用。
$(SLAVE_TARGET): $(COMMON_SRC) slave_main.c $(HDRS)
	$(CC) $(CFLAGS) $(CPPFLAGS) -DKV_SLAVE_BUILD \
	      slave_main.c $(COMMON_SRC) -o $@ $(LDLIBS)
	@echo "== 已构建 $@（从节点）网络层=$(NET) 存储层=$(STORE) =="

# ── kvbuf 的独立单测：只编 kvbuf.c + test_kvbuf.c，不碰服务器和网络层 ──
test_kvbuf: kvbuf.c test_kvbuf.c kvbuf.h
	$(CC) $(CFLAGS) $(CPPFLAGS) kvbuf.c test_kvbuf.c -o $@

# ── 测试客户端：复用 kvbuf.c，不依赖任何网络层 ──
test_client: test_client.c kvbuf.c kvbuf.h
	$(CC) $(CFLAGS) $(CPPFLAGS) test_client.c kvbuf.c -o $@

clean:
	rm -f $(TARGET) $(SLAVE_TARGET) test_kvbuf test_client

.PHONY: all clean test_kvbuf test_client