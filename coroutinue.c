#include "coroutinue.h"
#include "kvbuf.h"
#include "kv_repl.h" /* 顶部，给 kv_repl_on_close 用 */
#include "kv_net.h"

#define OUTQ_MAX 4096 /* 待发队列按 fd 索引，和 conn_list 同思路 */

typedef struct
{
    kvbuf q;     /* 正在发（或即将发）的数据 */
    kvbuf pend;  /* sending 期间别的协程推来的数据，先存这里 */
    int sending; /* 1 = 有协程正在往这个 fd 发 */
} outq_t;

static outq_t g_outq[OUTQ_MAX];

PROTOCOL protocol_handler = NULL;

int kv_net_push(int fd, const char *buf, size_t len)
{
    if (fd < 0 || fd >= OUTQ_MAX || len == 0)
        return -1;

    outq_t *o = &g_outq[fd];

    /* ① 有人正在发 → 只进 pend，立刻返回（不插手，避免字节交错） */
    if (o->sending)
        return (kvbuf_append(&o->pend, buf, len) == 0) ? 0 : -1;

    /* ② 没人发 → 入 q，我当唯一写者 */
    if (kvbuf_append(&o->q, buf, len) != 0)
        return -1;

    o->sending = 1;
    for (;;)
    {
        /* 把 q 发完（nty_send 内部会循环到发完，发不动就 yield） */
        while (o->q.len > 0)
        {
            ssize_t n = nty_send(fd, o->q.p, o->q.len, 0);
            if (n <= 0)
            {
                o->sending = 0;
                return -1;
            }

            if ((size_t)n < o->q.len)
            {
                memmove(o->q.p, o->q.p + n, o->q.len - (size_t)n);
                o->q.len -= (size_t)n;
            }
            else
            {
                o->q.len = 0;
            }
        }
        if (o->pend.len == 0)
            break; /* q 空了且无积压 → 结束 */

        kvbuf tmp = o->q; /* 换缓冲，零拷贝 */
        o->q = o->pend;
        o->pend = tmp;
    }
    o->sending = 0;
    return 0;
}

void kv_net_close(int fd)
{
    if (fd >= 0 && fd < OUTQ_MAX)
    {
        kvbuf_destroy(&g_outq[fd].q);
        kvbuf_destroy(&g_outq[fd].pend);
        g_outq[fd].sending = 0;
    }
    nty_close(fd);
}

static void deal_conn(void *arg)
{
    int fd = *(int *)arg;
    free(arg);

    kvbuf rbuf, sbuf;
    kvbuf_init(&rbuf);
    kvbuf_init(&sbuf);
    int as_slave = 0;        /* 这条连接是否已改由 kv_net_push 驱动 */
    while (1)
    {
        /* 1. 备空间 */
        if (kvbuf_reserve(&rbuf, rbuf.len + CONN_BUF_SIZE) != 0)
            break;

        /* 2. 收数据 */
        int n = nty_recv(fd, rbuf.p + rbuf.len, rbuf.cap - rbuf.len, 0);
        if (n <= 0)
            break;
        rbuf.len += n;

        /* 3. 切 + 处理（所有完整命令） */
        if (kvbuf_process(&rbuf, &sbuf, protocol_handler, fd) != 0)
            break;

        /* 4. 把这一批的应答一次性发出去 */
        if (sbuf.len > 0)
        {
            if (nty_send(fd, sbuf.p, sbuf.len, 0) != (ssize_t)sbuf.len)
                break;
            sbuf.len = 0; /* 清空，下一批复用这块内存 */
        }
        if (kv_repl_is_slave_fd(fd))
        {
            as_slave = 1;
            break;
        }
    }

    kvbuf_destroy(&rbuf);
    kvbuf_destroy(&sbuf);
        if (!as_slave)
    {
        kv_repl_on_close(fd);
        nty_close(fd);//只有不是从节点才关掉fd
    }
}

static void init_server(void *arg)
{
    unsigned short port = *(unsigned short *)arg;

    int listenfd = nty_socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0)
        return;

    struct sockaddr_in local;
    memset(&local, 0, sizeof(local));
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = htons(port);

    if (bind(listenfd, (struct sockaddr *)&local, sizeof(local)) < 0)
    {
        perror("bind");
        return;
    }
    if (listen(listenfd, 128) < 0)
    {
        perror("listen");
        return;
    }

    while (1)
    {
        int cli = nty_accept(listenfd, NULL, NULL); /* 阻塞在这，线程不闲置 */
        if (cli < 0)
            continue;

        int *p = malloc(sizeof(int));
        *p = cli;

        nty_coroutine *co;
        nty_coroutine_create(&co, deal_conn, p); /* 每连接一个协程 */
    }
}

int coroutinue_start(int port, PROTOCOL handler)
{

    nty_coroutine *co;
    nty_coroutine_create(&co, init_server, &port); /* 只入队 */
    protocol_handler = handler;
    nty_schedule_run(); /* 真正的 while(1) */
    return 0;
}