#include "kv_persist.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>      /* fsync */

#include "kvbuf.h"
#include "kv_foreach.h"
#include "kv_repl.h"

static FILE *g_aof_fp = NULL;

/* ── AOF 写入：一条写命令的原始字节，两个目的地 ──
   本地文件（主节点重启恢复）+ 所有从节点（主从同步）。 */
static void aof_append(const char *raw, size_t len)
{
    if (len == 0)
        return;

    if (g_aof_fp != NULL) {
        fwrite(raw, 1, len, g_aof_fp);

        /* fflush 每次都做：不刷的话数据还在 stdio 用户态缓冲里，
           kill -9 会丢。fsync 是另一层次（防断电），按策略做，不在这里。 */
        fflush(g_aof_fp);
    }

    kv_repl_broadcast(raw, len);      /* 第二个目的地：从节点 */
}

int kv_aof_open(const char *path)
{
    if (g_aof_fp != NULL)
        return 0;

    g_aof_fp = fopen(path, "ab");     /* ab = 追加 + 二进制，绝不截断 */
    if (g_aof_fp == NULL) {
        perror("fopen aof");
        return -1;
    }

    kv_set_aof_hook(aof_append);
    return 0;
}

int kv_aof_truncate(void)
{
    if (g_aof_fp != NULL) {
        fclose(g_aof_fp);             /* fclose 内部会 fflush */
        g_aof_fp = NULL;
    }

    FILE *fp = fopen(AOF_FILE, "wb"); /* "w" 打开即截断为 0 字节 */
    if (fp == NULL)
        return -1;
    fclose(fp);

    g_aof_fp = fopen(AOF_FILE, "ab"); /* 重新进入追加模式 */
    return (g_aof_fp != NULL) ? 0 : -1;
}

/* ── 遍历回调：把一条 kv 写成 SETB 命令 ──
   为什么用 SETB 而不是 SET：value 里可能有换行，只有带长度的格式才不会被撑破。 */
static int dump_one(char *k, char *v, void *arg)
{
    FILE  *fp   = (FILE *)arg;
    size_t vlen = strlen(v);

    if (fprintf(fp, "SETB %s %zu\n", k, vlen) < 0)
        return -1;                    /* 非 0 → 让遍历提前停下 */

    if (vlen > 0 && fwrite(v, 1, vlen, fp) != vlen)
        return -1;

    if (fputc('\n', fp) == EOF)       /* 正文后补换行，纯为文件可读 */
        return -1;

    return 0;
}

/* ── 全量落盘 ──
   写 tmp 再 rename：断电时要么是旧快照、要么是新快照，不会是"半个"。 */
int kv_dump(const char *path)
{
    char  tmp[512];
    FILE *fp;
    int   rc;

    snprintf(tmp, sizeof(tmp), "%s.tmp", path);

    fp = fopen(tmp, "wb");
    if (fp == NULL) {
        perror("fopen dump");
        return -1;
    }

    rc = kv_store_foreach(dump_one, fp);

    if (fflush(fp) != 0)                            rc = -1;  /* 用户态 → 内核 */
    if (rc == 0 && fsync(fileno(fp)) != 0)          rc = -1;  /* 内核 → 磁盘 */
    if (fclose(fp) != 0)                            rc = -1;

    if (rc != 0) {
        remove(tmp);
        return -1;
    }

    if (rename(tmp, path) != 0) {                   /* 原子替换 */
        perror("rename");
        remove(tmp);
        return -1;
    }
    return 0;
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
/* ── 从文件回放 ──
   核心：不写任何新的解析代码，整段丢给 kvbuf_process。 */
int kv_load(const char *path, int (*handler)(char *msg, char *response))
{
    FILE   *fp;
    kvbuf   rbuf, sbuf;
    char    buf[64 * 1024];
    size_t  n;
    int     rc = 0;

    fp = fopen(path, "rb");
    if (fp == NULL) {
        if (errno == ENOENT)
            return 0;                 /* 文件不存在 = 空数据集，不是错误 */
        perror("fopen load");
        return -1;
    }

    kvbuf_init(&rbuf);
    kvbuf_init(&sbuf);

    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
        if (kvbuf_append(&rbuf, buf, n) != 0) { rc = -1; break; }

        sbuf.len = 0;                 /* 回放时的应答没意义，丢弃 */

        /* conn_fd 传 -1：本次不在任何连接上下文里 */
        if (kvbuf_process(&rbuf, &sbuf, handler, -1) != 0) { rc = -1; break; }
    }

    if (rc == 0 && ferror(fp))
        rc = -1;

    /* 文件尾部有残缺记录要报出来，不能静默当成"加载成功" */
    if (rc == 0 && rbuf.len > 0) {
        fprintf(stderr, "kv_load: %s 末尾有 %zu 字节不完整\n", path, rbuf.len);
        rc = -1;
    }

    kvbuf_destroy(&rbuf);
    kvbuf_destroy(&sbuf);
    fclose(fp);
    return rc;
}