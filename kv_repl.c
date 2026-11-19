#include "kv_repl.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "kvbuf.h"
#include "kv_foreach.h"
#include "kv_net.h"
/* ══════════ 从节点表 ══════════ */

#define MAX_SLAVES 8
typedef struct {
    int   fd;        /* 从节点连接 */
} slave_t;

static slave_t g_slaves[MAX_SLAVES];
static int     g_slave_count = 0;
//不同网络层通用









/* ══════════ 主节点：发全量 ══════════ */

/* 遍历回调：把一条 kv 拼成 SETB 命令，追加到内存缓冲。
   和 dump_one 的唯一区别：那个写 FILE*，这个写 kvbuf。 */
static int sync_one(char *k, char *v, void *arg)
{
    kvbuf *b    = (kvbuf *)arg;
    char   head[64];
    size_t vlen = strlen(v);
    int    hn   = snprintf(head, sizeof(head), "SETB %s %zu\n", k, vlen);

    if (hn <= 0 || kvbuf_append(b, head, (size_t)hn) != 0) return -1;
    if (kvbuf_append(b, v, vlen) != 0)                     return -1;
    if (kvbuf_append(b, "\n", 1) != 0)                     return -1;
    return 0;
}

/* 生成完整快照并发给 fd。
   为什么先整块拼好再发：拼的全是内存操作、不 yield，
   所以拼出来的一定是"一个瞬间的一致状态"。 */
static int repl_full_sync(int fd)
{
    kvbuf buf;
    kvbuf_init(&buf);

    if (kv_store_foreach(sync_one, &buf) != 0) {
        kvbuf_destroy(&buf);
        return -1;
    }

    int rc = 0;
    if (buf.len > 0)
        rc = kv_net_push(fd, buf.p, buf.len);    /*  原来的 nty_send */

    kvbuf_destroy(&buf);
    return rc;

}

/* SYNC 命令的执行体：登记从节点 → 推全量 */
static int repl_sync_hook(int conn_fd,kvbuf *out)
{
    (void)out;   //显式告诉编译器“这个参数我是故意不用的”
                        /* 全量走 kv_net_push，不再用 sbuf */

    if (conn_fd < 0)                 return -1;
    if (g_slave_count >= MAX_SLAVES) return -1;

    /* 同一个 fd 重复发 SYNC：已登记过就忽略 */
    for (int i = 0; i < g_slave_count; i++)
        if (g_slaves[i].fd == conn_fd) return 0;

    /* ① 先登记。登记到这里只做一次赋值，不 yield，
          所以不会有广播漏在这个窗口里 */
    g_slaves[g_slave_count++].fd = conn_fd;

    /* ② 再推全量。push 内部 yield 期间到来的广播会追加到同一条队列 */
    if (repl_full_sync(conn_fd) != 0) {
        g_slave_count--;
        return -1;
    }

    printf("[master] 从节点 fd=%d 全量已入队\n", conn_fd);
    return 0;                    
}


int kv_repl_master_init(void)
{
    kv_set_sync_hook(repl_sync_hook);
    return 0;
}

/* ══════════ 主节点：广播增量 ══════════ */

/* 把一条写命令推给所有从节点 */
void kv_repl_broadcast(const char *raw, size_t len)
{
    for (int i = 0; i < g_slave_count; ) {
        if (kv_net_push(g_slaves[i].fd, raw, len) != 0) {
            printf("[master] 从节点 fd=%d 断开\n", g_slaves[i].fd);
            kv_net_close(g_slaves[i].fd);
            g_slaves[i] = g_slaves[--g_slave_count];
            continue;                 /* 不 i++，重查该位置 */
        }
        i++;
    }
}

void kv_repl_on_close(int fd)
{
    for (int i = 0; i < g_slave_count; ) {
        if (g_slaves[i].fd != fd) { i++; continue; }

        printf("[master] 从节点 fd=%d 关闭，移出列表\n", fd);
        g_slaves[i] = g_slaves[--g_slave_count];
        /* 不移 i：换过来的元素也要检查 */
    }
}

int kv_repl_is_slave_fd(int fd)
{
    for (int i = 0; i < g_slave_count; i++)
        if (g_slaves[i].fd == fd) return 1;
    return 0;
}

/*网络层协议判断*/
#if NET_ENGINE == NTYCO
#include <arpa/inet.h>
#include <sys/socket.h>
#include "coroutinue.h"
static int  g_is_slave    = 0;
static char g_master_ip[64] = {0};
static int  g_master_port   = 0;
static int (*g_protocol)(char *msg, char *response) = NULL;
/* ══════════ 从节点：拉数据 + 重放 ══════════ */



/*
 * 从节点主循环：连主节点 → 发 SYNC → 清库 → 持续接收重放 → 断开重连。
 * 它和 init_server 协程并发跑，所以从节点同时能对外提供读服务。
 */
static void repl_slave_coro(void *arg)
{
    (void)arg;

    while (1)
    {
        int fd = nty_socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) { nty_coroutine_sleep(3000); continue; }

        struct sockaddr_in a;
        memset(&a, 0, sizeof(a));
        a.sin_family = AF_INET;
        a.sin_port   = htons((unsigned short)g_master_port);
        inet_pton(AF_INET, g_master_ip, &a.sin_addr);

        if (nty_connect(fd, (struct sockaddr *)&a, sizeof(a)) < 0) {
            printf("[slave] 连不上 %s:%d，3 秒后重试\n", g_master_ip, g_master_port);
            nty_close(fd);
            nty_coroutine_sleep(3000);
            continue;
        }

        printf("[slave] 已连接主节点 %s:%d\n", g_master_ip, g_master_port);

        nty_send(fd, "SYNC\n", 5, 0);   /* 报到：请把全量给我 */
        kv_store_clear();                /* 先清空，再重灌，避免旧 key 残留 */

        kvbuf rbuf, sbuf;
        kvbuf_init(&rbuf);
        kvbuf_init(&sbuf);

        size_t total = 0;

        while (1) {
            if (kvbuf_reserve(&rbuf, rbuf.len + 4096) != 0) break;

            int n = nty_recv(fd, rbuf.p + rbuf.len, rbuf.cap - rbuf.len, 0);
            if (n <= 0) break;           /* 主节点断开 */

            rbuf.len += (size_t)n;
            total    += (size_t)n;
            sbuf.len  = 0;               /* 重放时的应答没意义 */

            /* ★ 和 kv_load 读文件、和主节点收客户端命令，同一个函数 */
            if (kvbuf_process(&rbuf, &sbuf, g_protocol, fd) != 0) break;
        }

        printf("[slave] 与主节点断开，本次收到 %zu 字节\n", total);

        kvbuf_destroy(&rbuf);
        kvbuf_destroy(&sbuf);
        nty_close(fd);

        nty_coroutine_sleep(3000);       /* 3 秒后重连 */
    }
}

int kv_repl_slave_start(const char *master_ip, int master_port,
                        int (*handler)(char *msg, char *response))
{
    if (master_ip == NULL || handler == NULL)
        return -1;

    g_is_slave    = 1;
    g_protocol    = handler;
    g_master_port = master_port;
    snprintf(g_master_ip, sizeof(g_master_ip), "%s", master_ip);

    /* 只是入队；真正的调度在 coroutinue_start 里的 nty_schedule_run() */
    nty_coroutine *co;
    nty_coroutine_create(&co, repl_slave_coro, NULL);
    return 0;
}

int kv_repl_is_slave(void) { return g_is_slave; }

#else  /* ══════ 非协程层：全部空实现，保证编译通过 ══════ */

int kv_repl_slave_start(const char *ip, int port,
                        int (*handler)(char *msg, char *response))
{
    (void)ip; (void)port; (void)handler;
    fprintf(stderr, "[repl] 主从同步只在 NET=ntyco 下可用，--slave 已忽略\n");
    return -1;
}

int kv_repl_is_slave(void) { return 0; }


#endif /* NET_ENGINE == NTYCO */