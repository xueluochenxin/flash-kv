# flash-kv

纯 C 实现的 TCP Key-Value 存储服务器，采用**可插拔**的「网络引擎 × 存储引擎」结构，
并在其上叠加了内存池、持久化（全量快照 + 增量 AOF）与主从同步。

## 统一协议

每条命令以 `\n` 结束，响应也以 `\n` 结束。

| 命令 | 成功响应 | 失败响应 | 说明 |
|---|---|---|---|
| `SET key value` | `OK` | `WRONG` | key 已存在时直接修改其 value |
| `GET key` | `value` | `INEXIST` | key 不存在 |
| `DEL key` | `OK` | `INEXIST` | key 不存在 |
| `MOD key value` | `OK` | `INEXIST` | 仅修改已存在的 key |
| `EXIST key` | `YES` | `NO` | |
| `SETB key vlen` | `OK` | `INVALID` / `WRONG` | **块帧**：头部行之后紧跟 `vlen` 个原始字节，可含 `\n` |
| `GETB key` | `$vlen` + 正文 | `INEXIST` | 块帧应答：先一行 `$vlen`，再 `vlen` 字节正文，末尾补一个 `\n` |
| `SAVE` | `OK` | `WRONG` | 主节点：全量落盘，随后清空 AOF |
| `SYNC` | （无应答） | — | 主节点：把整个数据集推给该连接，并将它登记为从节点 |

解析规则：

- 前两个字段按空白切分，**第三个字段取整行剩余部分**，因此 value 可以包含空格
- `SET` / `MOD` 的 value 不能含换行；需要换行请用 `SETB`
- 值里含 `\0` 时返回 `INVALID`（文本协议 + `strlen` 链路的限制）
- 空行或全空白行返回 `ERROR`；参数不足或未知命令返回 `INVALID`

协议原文见 [`information.txt`](information.txt)。

## 架构

入口 [`flash_kvstore.c`](flash_kvstore.c) 通过两个宏在编译期决定组装方式（由 Makefile 用 `-D` 传入）：

- `NET_ENGINE`：`NTYCO` / `REACTOR` / `PROACTOR`
- `STORE_ENGINE`：`ARRAY_S` / `RBTREE_S` / `HASH_S` / `SKIPLIST_S`

```
client ──TCP──> 网络层（ntyco / reactor / proactor）
                    │  recv → rbuf
                    ▼
              kvbuf_process()      ← 分帧、块帧转换、AOF 钩子、SYNC 钩子
                    │  一条完整命令
                    ▼
              protocol()           ← SAVE 等服务器级命令
                    │
                    ▼
              kvs_<engine>_protocol()
                    │
                    ▼
              存储引擎（array / rbtree / hashmap / skiplist）
```

三条横切的公共通道，让上层逻辑与「用哪个网络引擎 / 哪个存储引擎」解耦：

| 通道 | 接口 | 说明 |
|---|---|---|
| 存储层遍历 | `kv_store_foreach()` | 四个引擎各实现一份；dump 与全量同步共用 |
| 清空数据集 | `kv_store_clear()` | 从节点接收全量前调用 |
| 跨连接推送 | `kv_net_push()` / `kv_net_close()` | 三层各实现一份；主从广播靠它，复制逻辑里不出现任何具体网络 API |

## 文件说明

| 文件 | 作用 |
|---|---|
| `flash_kvstore.c` | 服务器层：`protocol()`、遍历/清库/建引擎的分发、主节点 `main` |
| `slave_main.c` | 从节点入口（独立二进制） |
| `kvbuf.c` / `.h` | 每连接动态缓冲、分帧、块帧（SETB/GETB）解析、AOF 与 SYNC 钩子 |
| `kv_net.h` | 跨连接推送契约：`kv_net_push` / `kv_net_close` |
| `kv_persist.c` / `.h` | 全量 dump / load、AOF 写入与截断 |
| `kv_repl.c` / `.h` | 主从同步：从节点表、全量推送、增量广播；从节点侧的连接与重放 |
| `coroutinue.c` / `.h` | NtyCo 协程网络层；同时实现 ntyco 版 `kv_net_push` |
| `reactor.c` / `.h` | epoll 反应堆网络层；实现 reactor 版 `kv_net_push` |
| `proactor.c` / `.h` | io_uring 前摄器网络层；实现 proactor 版 `kv_net_push` |
| `array.c` / `RBTREE.c` / `hashmap.c` / `skiplist.c` | 四个存储引擎，各含 `protocol` / `foreach` / `clear` |
| `kvmemctl.c` / `.h` | size-class 内存池（`kv_malloc` / `kv_free`） |
| `kv_foreach.h` | 存储层公共接口声明 |
| `Makefile` | 构建脚本，支持 `NET=` / `STORE=` 切换 |
| `information.txt` | 协议与设计说明 |
| `nty_*.c` / `.h` | NtyCo 源码（协程、调度、epoll、socket hook） |

## 构建

```sh
make                          # ntyco + skiplist，产出 flash_kv 与 flash_kv_slave
make NET=reactor              # 换网络层
make NET=proactor             # 需要 liburing-dev
make STORE=HASH_S             # 换存储引擎
make NET=proactor STORE=ARRAY_S
make clean
```

`make` 会生成两个二进制：

| 二进制 | 入口 | 角色 |
|---|---|---|
| `flash_kv` | `flash_kvstore.c` 里的 `main` | 主节点 |
| `flash_kv_slave` | `slave_main.c` 里的 `main` | 从节点 |

两者共用同一份代码：编从节点时加 `-DKV_SLAVE_BUILD`，把 `flash_kvstore.c` 里的 `main` 条件编译掉，入口改由 `slave_main.c` 提供；`protocol()`、遍历、清库等仍然完全复用。

**改 `NET` / `STORE` 之后必须先 `make clean`**（目标文件同名）。

## 运行

### 主节点

```sh
./flash_kv 8888
```

### 从节点

```sh
./flash_kv_slave <本机监听端口> <主节点IP> <主节点端口>
./flash_kv_slave 8889 192.168.74.128 8888
```

从节点启动后：连主节点 → 发 `SYNC` → 清空本地 → 重放全量 → 持续重放增量；断线 3 秒后自动重连。同一进程内还会照常 `accept` 客户端连接，对外提供读服务。

### 数据文件

```
./data/snapshot.aof     全量快照（SAVE 生成；tmp + fsync + rename 原子替换）
./data/appendonly.aof   增量日志（每条写命令的原始字节）
```

主节点启动顺序：先读快照，再重放 AOF，**加载完成后**才注册 AOF 钩子（否则回放会把日志再写一遍）。从节点不读这两个文件，数据全部由主节点推过来。

## 已知限制

| 限制 | 说明 |
|---|---|
| 从节点只支持 ntyco | reactor / proactor 的从节点需要把阻塞式复制循环改写成事件驱动状态机；启动时会明确报错退出 |
| 从节点未做只读拦截 | 客户端写从节点会真的写进本地引擎，主节点下次全量时覆盖 |
| 断线只能重新全量 | 没有 offset / 环形缓冲，重连即重灌 |
| 主节点靠发送失败发现从节点断开 | 惰性检测，无心跳 |
| 值里不能含 `\0` | 文本协议与 `strlen` 链路的限制 |
| 单线程 | 全量 dump、全量推送期间会阻塞其他请求 |