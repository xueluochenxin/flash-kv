#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>   /* fsync()  */
#include <sys/stat.h> /* mkdir()  */

#include "kvbuf.h"      /* kv_load 里要用 kvbuf_process */
#include "kv_foreach.h" /* dump 里要用 kv_store_foreach */
static FILE *g_aof_fp = NULL;
/////////////////////////////////////////////** */////////////////////////////////////////////
#define DATA_DIR "./data"
#define SNAPSHOT DATA_DIR "/snapshot.aof"
#define AOF_FILE DATA_DIR "/appendonly.aof"

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
static void aof_append(const char *raw, size_t len)
{
    if (g_aof_fp == NULL || len == 0)
        return;

    fwrite(raw, 1, len, g_aof_fp);

    /*  必须 fflush：
       数据还在 stdio 的用户态缓冲里，进程被 kill -9 就丢了
       （fsync 是另一个层次，防的是"断电"） */
    fflush(g_aof_fp);
}
static int aof_truncate(void) // 把aof清空，重新进入追加状态
{
    if (g_aof_fp != NULL)
    {
        fclose(g_aof_fp);
        g_aof_fp = NULL;
    }

    FILE *fp = fopen(AOF_FILE, "wb"); /* 打开即截断 */
    if (fp == NULL)
        return -1;
    fclose(fp);

    g_aof_fp = fopen(AOF_FILE, "ab"); /* 重新以追加方式打开 */
    return (g_aof_fp != NULL) ? 0 : -1;
}
static int dump_one(char *k, char *v, void *arg)
{
    FILE *fp = (FILE *)arg;
    size_t vlen = strlen(v);

    /*  头部是文本 */
    if (fprintf(fp, "SETB %s %zu\n", k, vlen) < 0)
        return -1; /* 非 0 → 让遍历停下 */

    /* 正文是原始字节，value 里可能有 '\n'，必须 fwrite */
    if (vlen > 0 && fwrite(v, 1, vlen, fp) != vlen)
        return -1;

    if (fputc('\n', fp) == EOF)
        return -1;
    return 0; /* 0 = 继续遍历下一条 */
}
//kvdump函数梳理
/*
我们一开始拼出来的其实是 "data/snapshot.aof.tmp"
新建一个文件就是为了避免刚打开快照突然崩溃，然后数据全丢失了
所以我们创立了个临时文件出来，最后原子操作rename改名


三层缓冲
你的程序                   内核                    磁盘
┌──────────────┐      ┌──────────────┐      ┌──────────────┐
│ stdio 缓冲    │      │ 页缓存        │      │  物理盘片     │
│ (FILE* 内部)  │      │ (page cache) │      │              │
└──────────────┘      └──────────────┘      └──────────────┘
       │                     │                     │
       │  fflush()           │                     │
       └────────────────────→│                     │
                             │  fsync()            │
                             └────────────────────→│

kill -9：程序缓冲丢 → 必须 fflush
断电：内核页缓存丢 → 必须 fsync
*/
int kv_dump(const char *path)
{
    char tmp[512];
    FILE *fp;
    int rc;

    snprintf(tmp, sizeof(tmp), "%s.tmp", path);

    fp = fopen(tmp, "wb"); /* wb：二进制写，不做换行转换 */
    if (fp == NULL)
    {
        perror("fopen dump");
        return -1;
    }

    rc = kv_store_foreach(dump_one, fp); /* ← 遍历接口在这里被用到 */

    if (fflush(fp) != 0)
        rc = -1;
    if (rc == 0 && fsync(fileno(fp)) != 0)//把内核页缓存里的脏数据真正写到磁盘，并等待写完成才返回
        rc = -1;
    if (fclose(fp) != 0)
        rc = -1;

    if (rc != 0)
    {
        remove(tmp); /* 失败就删掉，别留垃圾 */
        return -1;
    }

    if (rename(tmp, path) != 0)
    { /* 原子替换 */
        perror("rename");
        remove(tmp);
        return -1;
    }
    return 0;
}
int kv_load(const char *path, int (*handler)(char *msg, char *response))
{
    FILE *fp;
    kvbuf rbuf, sbuf;
    char buf[64 * 1024];
    size_t n;
    int rc = 0;

    fp = fopen(path, "rb");
    if (fp == NULL)
    {
        if (errno == ENOENT)
            return 0; /* 文件不存在 = 空，不是错误 */
        perror("fopen load");
        return -1;
    }

    kvbuf_init(&rbuf);
    kvbuf_init(&sbuf);

    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
    {
        if (kvbuf_append(&rbuf, buf, n) != 0)
        {
            rc = -1;
            break;
        }

        sbuf.len = 0; /* 加载时的应答没有意义，丢弃 */

        if (kvbuf_process(&rbuf, &sbuf, handler) != 0)
        {
            rc = -1;
            break;
        }
    }

    if (rc == 0 && ferror(fp))
        rc = -1;

    /* 文件末尾如果有残缺的记录，要报出来，不能静默忽略 */
    if (rc == 0 && rbuf.len > 0)
    {
        fprintf(stderr, "kv_load: %s 末尾有 %zu 字节不完整\n", path, rbuf.len);
        rc = -1;
    }

    kvbuf_destroy(&rbuf);
    kvbuf_destroy(&sbuf);
    fclose(fp);
    return rc;
}
int protocol(char *msg, char *response)
{
    if (strncmp(msg, "SAVE", 4) == 0 && (msg[4] == '\0' || msg[4] == ' '))
    {
        if (kv_dump(SNAPSHOT) != 0)
            sprintf(response, "WRONG\n");
        else
        {
            aof_truncate();
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

    if (kv_load(AOF_FILE, protocol) != 0) {
        fprintf(stderr, "加载 AOF 失败: %s\n", AOF_FILE);
        return -1;
    }

    /*  加载全部完成，才打开 AOF 并挂钩子 */
    g_aof_fp = fopen(AOF_FILE, "ab");
    if (g_aof_fp == NULL) {
        perror("fopen " AOF_FILE);
        return -1;
    }
    kv_set_aof_hook(aof_append);
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