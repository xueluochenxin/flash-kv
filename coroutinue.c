#include "coroutinue.h"
#include "kvbuf.h"
static size_t g_max_cmd_len = 256; // 记录一个目前见过的最长的长度，便于协议处理给返回值

PROTOCOL protocol_handler = NULL;
static void deal_conn(void *arg)
{
    int fd = *(int *)arg;
    free(arg);

    kvbuf rbuf, sbuf;
    kvbuf_init(&rbuf);
    kvbuf_init(&sbuf);

    int err = 0;

    while (!err)
    {
        /* ── 1. 保证 rbuf 尾部至少有 CONN_BUF_SIZE 字节可写 ── */
        if (kvbuf_reserve(&rbuf, rbuf.len + CONN_BUF_SIZE) != 0)
        {
            err = 1;
            break;
        }

        int n = nty_recv(fd, rbuf.p + rbuf.len, rbuf.cap - rbuf.len, 0);
        if (n <= 0)
            break; /* 0 = 对端关闭，<0 = 出错 */
        rbuf.len += n;

        /* ── 循环切出所有完整的行 ── */
        size_t start = 0;

        while (1)
        {
            char *base = rbuf.p + start;
            size_t avail = rbuf.len - start;

            char *nl = memchr(base, '\n', avail);
            if (nl == NULL)
                break; /* 还没收完整，回去继续 recv */

            size_t len = (size_t)(nl - base);
            *nl = '\0'; /* 原地截断：把 '\n' 换成 '\0' */

            if (len == 0)
            { /* 空行，跳过 */
                start += 1;
                continue;
            }

            if (len + 64 > g_max_cmd_len)
                g_max_cmd_len = len + 64;
            /* 应答缓冲：短应答最多几十字节，留 64 字节余量 */
            if (kvbuf_reserve(&sbuf, len + 64) != 0)
            {
                err = 1;
                break;
            }

            int num = protocol_handler(base, sbuf.p);
            if (num <= 0)
            {
                err = 1;
                break;
            }
            if (nty_send(fd, sbuf.p, num, 0) != num)
            {
                err = 1;
                break;
            }

            start += len + 1; /* 连同 '\n' 一起消费掉 */
        }

        if (err)
            break;

        /* ── 4. 把已经处理过的字节搬到开头，剩下的留到下次 ── */
        if (start > 0)
        {
            memmove(rbuf.p, rbuf.p + start, rbuf.len - start);
            rbuf.len -= start;
        }
    }

    kvbuf_destroy(&rbuf);
    kvbuf_destroy(&sbuf);
    nty_close(fd);
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