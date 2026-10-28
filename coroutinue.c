#include "coroutinue.h"

PROTOCOL protocol_handler=NULL;
static void deal_conn(void *arg)
{
    int fd = *(int *)arg;
    free(arg);

    char bufrecv[1024]={0};
    char bufsend[1024]={0};
    while(1) {
        int n = nty_recv(fd, bufrecv, sizeof(bufrecv) - 1, 0);
        if (n <= 0)                       /* 0 = 对端关闭，<0 = 出错 */
            break;

        bufrecv[n] = '\0';     
        int num=protocol_handler(bufrecv,bufsend);
        if(num<0)
        {
            break;
        }
        if (nty_send(fd, bufsend, num, 0) != num) 
            break;
    }

    nty_close(fd);
}

static void init_server(void *arg)
{
    unsigned short port = *(unsigned short *)arg;

    int listenfd = nty_socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) return;

    struct sockaddr_in local;
    memset(&local, 0, sizeof(local));
    local.sin_family      = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port        = htons(port);

    if (bind(listenfd, (struct sockaddr *)&local, sizeof(local)) < 0) {
        perror("bind"); return;
    }
    if (listen(listenfd, 128) < 0) {
        perror("listen"); return;
    }


    while(1) {
        int cli = nty_accept(listenfd, NULL, NULL);   /* 阻塞在这，线程不闲置 */
        if (cli < 0) continue;

        int *p = malloc(sizeof(int));
        *p = cli;

        nty_coroutine *co;
        nty_coroutine_create(&co, deal_conn, p);      /* 每连接一个协程 */
    }
}

int coroutinue_start(int port, PROTOCOL handler)
{


    nty_coroutine *co;
    nty_coroutine_create(&co, init_server, &port);    /* 只入队 */
    protocol_handler=handler;
    nty_schedule_run();                               /* 真正的 while(1) */
    return 0;
}