/* 从节点入口。
   和主节点的区别：不读本地文件、不挂 AOF 钩子、不注册 SYNC 钩子，
   只启动"同步协程 + 网络层"。 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>    /* mkdir */

#include "kvbuf.h"
#include "kv_foreach.h"
#include "kv_persist.h"  /* DATA_DIR */
#include "kv_repl.h"

/* protocol() 定义在 flash_kvstore.c，这里只需要声明 */
extern int protocol(char *msg, char *response);

#define REACTOR 1
#define PROACTOR 2
#define NTYCO 3

#define ARRAY_S 1
#define RBTREE_S 2
#define HASH_S 3
#define SKIPLIST_S 4

#ifndef NET_ENGINE
#define NET_ENGINE NTYCO
#endif

#ifndef STORE_ENGINE
#define STORE_ENGINE SKIPLIST_S
#endif

#if NET_ENGINE == REACTOR
#include "reactor.h"
#elif NET_ENGINE == PROACTOR
#include "proactor.h"
#elif NET_ENGINE == NTYCO
#include "coroutinue.h"
#endif

#if STORE_ENGINE == ARRAY_S
#include "array.h"
#elif STORE_ENGINE == RBTREE_S
#include "RBTREE.h"
#elif STORE_ENGINE == HASH_S
#include "hashmap.h"
#elif STORE_ENGINE == SKIPLIST_S
#include "skiplist.h"
#endif

#ifndef KV_FREE
#include "kvmemctl.h"
#endif

int main(int argc, char **argv)
{
    if (argc != 4)
    {
        fprintf(stderr, "用法: %s <本机监听端口> <主节点IP> <主节点端口>\n", argv[0]);
        return -1;
    }

    int         port        = atoi(argv[1]);
    const char *master_ip   = argv[2];
    int         master_port = atoi(argv[3]);

    printf("[startup] 从节点模式，监听 %d，主节点 %s:%d\n",
           port, master_ip, master_port);

    kv_mempool_init();
    if (mkdir(DATA_DIR, 0755) != 0 && errno != EEXIST)
    {
        perror("mkdir " DATA_DIR);
        return -1;
    }

    kv_store_engine_create();        /* 和主节点用同一套引擎创建 */

    /* 启动同步协程。必须在 coroutinue_start 之前：它只是入队 */
    if (kv_repl_slave_start(master_ip, master_port, protocol) != 0)
    {
        fprintf(stderr, "启动从节点同步失败（当前网络层不支持）\n");
        return -1;
    }

    /* 网络层启动（死循环）。从节点同时对外提供读服务 */
#if (NET_ENGINE == REACTOR)
    reactor_start(port, protocol);
#elif (NET_ENGINE == PROACTOR)
    proactor_start(port, protocol);
#elif NET_ENGINE == NTYCO
    coroutinue_start(port, protocol);
#endif

#if (STORE_ENGINE == ARRAY_S)
    kv_array_distory(kv_array);
#elif (STORE_ENGINE == RBTREE_S)
    RBTREE_destroy(T);
#elif (STORE_ENGINE == HASH_S)
    hashmap_destroy(hash);
#elif (STORE_ENGINE == SKIPLIST_S)
    skiplist_destroy(sl);
#endif
    kv_mempool_destroy();
    return 0;
}