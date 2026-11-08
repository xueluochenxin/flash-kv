#include "coroutinue.h"
#include "kvbuf.h"

PROTOCOL protocol_handler = NULL;
static void deal_conn(void *arg)
{
    int fd = *(int *)arg;
    free(arg);

    kvbuf rbuf, sbuf;
    kvbuf_init(&rbuf);
    kvbuf_init(&sbuf);

    while (1) {
        /* 1. 备空间 */
        if (kvbuf_reserve(&rbuf, rbuf.len + CONN_BUF_SIZE) != 0)
            break;

        /* 2. 收数据 */
        int n = nty_recv(fd, rbuf.p + rbuf.len, rbuf.cap - rbuf.len, 0);
        if (n <= 0)
            break;
        rbuf.len += n;

        /* 3. 切 + 处理（所有完整命令） */
        if (kvbuf_process(&rbuf, &sbuf, protocol_handler) != 0)
            break;

        /* 4. 把这一批的应答一次性发出去 */
        if (sbuf.len > 0) {
            if (nty_send(fd, sbuf.p, sbuf.len, 0) != (ssize_t)sbuf.len)
                break;
            sbuf.len = 0;               /* 清空，下一批复用这块内存 */
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