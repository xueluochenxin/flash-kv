# flash-kv

纯 C 实现的 TCP Key-Value 存储服务器，采用**可插拔**的「网络引擎 × 存储引擎」结构，对外提供统一的文本协议。

## 统一协议

每条命令一行：`COMMAND [key] [value]`，服务端返回单个响应词：

| 命令 | 成功响应 | 失败响应 | 说明 |
|---|---|---|---|
| `SET key value` | `OK` | `WRONG` | key 已存在时直接修改其 value |
| `GET key` | `value` | `INEXIST` | key 不存在 |
| `DEL key` | `OK` | `INEXIST` | key 不存在 |
| `MOD key value` | `OK` | `INEXIST` | 仅修改已存在的 key |
| `EXIST key` | `YES` | `NO` | |

无效输入一律返回 `INVALIDE`。协议原文见 [`information.txt`](information.txt)。

## 架构

入口 [`flash_kvstore.c`](flash_kvstore.c) 通过两个宏在编译期决定组装方式：

- `NET_ENGINE`：`REACTOR` / `PROACTOR` / `NTYCO`（NTYCO 预留）
- `STORE_ENGINE`：`ARRAY_S` / `RBTREE_S` / `HASH_S` / `SKIPLIST_S`

每个存储引擎对外暴露统一的 `kvs_<engine>_protocol(msg, response)` 接口；网络引擎负责 TCP 收发，把每条请求交给 `protocol()` 处理并回送响应。`KV_FREE` 未定义时自动引入 `kvmemctl` 做 KV 内存管理。

```
client ──TCP──> reactor / proactor ──> protocol() ──> 存储引擎
                                        array / rbtree / hashmap / skiplist
```

## 文件说明

| 文件 | 作用 |
|---|---|
| `flash_kvstore.c` | 入口：选择网络/存储引擎，`protocol()` 分发 |
| `reactor.c` / `reactor.h` | Reactor 网络引擎（当前默认启用） |
| `proactor.c` / `proactor.h` | Proactor 网络引擎 |
| `array.c` / `array.h` | 数组存储引擎 |
| `RBTREE.c` / `RBTREE.h` | 红黑树存储引擎 |
| `hashmap.c` / `hashmap.h` | 哈希表存储引擎 |
| `skiplist.c` / `skiplist.h` | 跳表存储引擎 |
| `kvmemctl.c` / `kvmemctl.h` | KV 内存管理 |
| `Makefile` | 构建脚本（gcc，`-std=gnu11 -g -O0 -Wall -Wextra`） |
| `information.txt` | 协议与设计说明 |

## 构建与运行

Linux + gcc 环境（本项目在 Linux 虚拟机上开发编译）：

```sh
make                # 生成可执行文件 flash_kv
./flash_kv <port>   # 在指定端口启动 KV 服务
make clean
```

