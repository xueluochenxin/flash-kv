#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>   /* fsync()  */
#include <sys/stat.h> /* mkdir()  */

#include "kvbuf.h"      /* kv_load 里要用 kvbuf_process */
#include "kv_foreach.h" /* dump 里要用 kv_store_foreach */
#include "kv_persist.h" /* kv_dump / kv_load / kv_aof_open */
#include "kv_repl.h"    /* kv_repl_master_init / kv_repl_slave_start / kv_repl_is_slave */
// static FILE *g_aof_fp = NULL;
/////////////////////////////////////////////** */////////////////////////////////////////////

#define REACTOR 1
#define PROACTOR 2
#define NTYCO 3

#define ARRAY_S 1
#define RBTREE_S 2
#define HASH_S 3
#define SKIPLIST_S 4

/* 由 Makefile 用 -DNET_ENGINE=... / -DSTORE_ENGINE=... 指定；
   如果直接 gcc 编译没带 -D，就回退到这里的默认值 */
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
////

int protocol(char *msg, char *response)
{

    if (strncmp(msg, "SAVE", 4) == 0 && (msg[4] == '\0' || msg[4] == ' '))
    {
        if (kv_dump(SNAPSHOT) != 0)
            sprintf(response, "WRONG\n");
        else
        {
            kv_aof_truncate();
            sprintf(response, "OK\n");
        }

        return (int)strlen(response);
    }

#if (STORE_ENGINE == ARRAY_S)
    return kvs_array_protocol(msg, response);
#elif (STORE_ENGINE == RBTREE_S)
    return kvs_rbtree_protocol(msg, response);
#elif (STORE_ENGINE == HASH_S)
    return kvs_hash_protocol(msg, response);
#elif (STORE_ENGINE == SKIPLIST_S)
    return kvs_skiplist_protocol(msg, response);
#endif
}
int kv_store_foreach(kv_visit_fn fn, void *arg)
{
#if STORE_ENGINE == ARRAY_S
    return kvs_array_foreach(fn, arg);
#elif STORE_ENGINE == RBTREE_S
    return kvs_rbtree_foreach(fn, arg);
#elif STORE_ENGINE == HASH_S
    return kvs_hash_foreach(fn, arg);
#elif STORE_ENGINE == SKIPLIST_S
    return kvs_skiplist_foreach(fn, arg);
#endif
}
void kv_store_clear(void)
{
#if STORE_ENGINE == ARRAY_S
    kv_array_distory(kv_array);
    kv_array_create(&kv_array);
#elif STORE_ENGINE == RBTREE_S
    rbtree_clear(T);
#elif STORE_ENGINE == HASH_S
    hashmap_clear(hash);
#elif STORE_ENGINE == SKIPLIST_S
    skiplist_clear(sl);
#endif
}
int main(int argc, char **argv)
{
    if (argc != 2)
    {
        return -1;
    }
    int port = atoi(argv[1]);
    kv_mempool_init();
    if (mkdir(DATA_DIR, 0755) != 0 && errno != EEXIST)
    {
        perror("mkdir " DATA_DIR);
        return -1;
    }

#if STORE_ENGINE == ARRAY_S
    kv_array_create(&kv_array);
#elif STORE_ENGINE == RBTREE_S
    T = RBTREE_create();
#elif (STORE_ENGINE == HASH_S)
    hash = hashmap_create(0);
#elif (STORE_ENGINE == SKIPLIST_S)
    sl = skiplist_create(0);
#endif

    if (kv_load(SNAPSHOT, protocol) != 0)
    {
        fprintf(stderr, "加载快照失败: %s\n", SNAPSHOT);
        return -1;
    }
    printf("[startup] 已加载 %s\n", SNAPSHOT);

    if (kv_load(AOF_FILE, protocol) != 0)
    {
        fprintf(stderr, "加载 AOF 失败: %s\n", AOF_FILE);
        return -1;
    }
    if (kv_aof_open(AOF_FILE) != 0)
    {
        return -1;
    }

    kv_repl_master_init();          /* 注册 SYNC 钩子 */
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
}