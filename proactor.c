// 异步 两者之间的关系
/*
 */
#include "proactor.h"

PROTOCOL g_protocol = NULL;

int set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int init_server(unsigned short port) // socket bind listen
{
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        perror("socket");
        return -1;
    }
    struct sockaddr_in server_add;
    memset(&server_add, 0, sizeof(struct sockaddr_in));
    server_add.sin_family = AF_INET;
    server_add.sin_addr.s_addr = htonl(INADDR_ANY);
    server_add.sin_port = htons(port);

    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)); // 端口复用
    if (bind(sockfd, (struct sockaddr *)&server_add, sizeof(struct sockaddr_in)))
    {
        close(sockfd);
        perror("bind");
        return -1;
    }

    if (listen(sockfd, 1024) < 0)
    {
        perror("listen");
        close(sockfd);
        return -1;
    }
    set_nonblocking(sockfd);
    return sockfd;
}

struct io_uring_sqe *get_sqe(struct io_uring *ring)
{
    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);

    if (sqe == NULL)
    {
        fprintf(stderr, "SQE 获取失败，队列已满\n");
    }
    return sqe;
}

int set_event_accept(struct io_uring *ring, int sockfd, struct sockaddr *addr,
                     socklen_t *addrlen, int flags)
{
    struct io_uring_sqe *sqe = get_sqe(ring);
    if (sqe == NULL)
    {
        return -1;
    }

    struct conn_info *info = malloc(sizeof(struct conn_info));
    if (info == NULL)
    {
        return -1;
    }
    memset(info, 0, sizeof(struct conn_info));
    info->fd = sockfd;
    info->event = EVENT_ACCEPT;
    info->readlen = 0;
    info->writelen = 0;

    io_uring_prep_accept(sqe, sockfd, addr, addrlen, flags);

    io_uring_sqe_set_data(sqe, info);
    return 0;
}
int set_event_recv(struct io_uring *ring, struct conn_info *info)
{
    if (info == NULL)
    {
        return -1;
    }
    struct io_uring_sqe *sqe;
    sqe = get_sqe(ring);
    if (sqe == NULL)
    {
        return -1;
    }
    info->event = EVENT_READ;
    info->readlen = 0;
    io_uring_prep_recv(sqe, info->fd, info->readbuf, BUFFER_SIZE - 1, 0);

    io_uring_sqe_set_data(sqe, info);
    return 0;
}
int set_event_send(struct io_uring *ring, struct conn_info *info)
{
    struct io_uring_sqe *sqe;

    if (info == NULL)
        return -1;

    sqe = get_sqe(ring);
    if (sqe == NULL)
        return -1;

    info->event = EVENT_WRITE;

    io_uring_prep_send(sqe,
                       info->fd,
                       info->writebuf,
                       (size_t)info->writelen,
                       0);
    io_uring_sqe_set_data(sqe, info);
    return 0;
}
void conn_close(struct conn_info *info)
{
    if (info == NULL)
        return;
    close(info->fd);
    // if(info->writebuf!=NULL)
    // {
    //     free(info->readbuf);
    // }
    // if(info->writebuf!=NULL)
    // {
    //     free(info->writebuf);
    // }     这是数组，不是malloc出来的，不需要单独free
    free(info);
}

int proactor_start(int port, PROTOCOL handler)
{

    int sockfd = init_server(port);
    g_protocol = handler;
    //
    struct io_uring_params params;
    memset(&params, 0, sizeof(struct io_uring_params));

    struct io_uring ring;
    io_uring_queue_init_params(ENTRIES_LENGTH, &ring, &params);

    // struct io_uring_sqe* sqe;
    // sqe=io_uring_get_sqe(&ring);
    //

    struct sockaddr_in client_addr;
    socklen_t lensock = sizeof(struct sockaddr_in);
    // struct conn_info accept_info;
    // accept_info.event=EVENT_ACCEPT;
    // accept_info.fd=sockfd;
    // accept_info.buf=NULL;

    set_event_accept(&ring, sockfd, (struct sockaddr *)&client_addr, &lensock, 0);
    struct io_uring_cqe *cqes[128];

    while (1)
    {
        int ret = io_uring_submit_and_wait(&ring, 1); // 至少等一个事件

        if (ret < 0)
        {
            perror("io_uring_submit_and_wait");
            break;
        }

        struct io_uring_cqe *cqe;
        // io_uring_wait_cqe(&ring,&cqe);

        int nready = io_uring_peek_batch_cqe(&ring, cqes, 128);
        for (int i = 0; i < nready; ++i)
        {
            struct io_uring_cqe *cqe = cqes[i];
            struct conn_info *result = (struct conn_info *)io_uring_cqe_get_data(cqe);

            if (result == NULL)
            {
                continue;
            }
            int res = cqe->res;
            if (result->event == EVENT_ACCEPT)
            {
                set_event_accept(&ring, sockfd, (struct sockaddr *)&client_addr, &lensock, 0);
                free(result);
                result = NULL;
                if (res > 0)
                {

                    struct conn_info *info = (struct conn_info *)calloc(1, sizeof(struct conn_info));
                    if (info == NULL)
                    {
                        return -1;
                    }
                    // info->event=EVENT_READ;
                    info->fd = res;
                    if (set_event_recv(&ring, info) != 0)
                    {
                        conn_close(info);
                    }
                    // printf("accept finished\n");
                }
                else
                {
                    //set_event_accept(&ring, sockfd, (struct sockaddr *)&client_addr, &lensock, 0);
                }
                // free(result);
            }
            else if (result->event == EVENT_READ)
            {

                if (res > 0) // res=实际读到的字节数
                {
                    result->readlen = res;
                    result->readbuf[res] = '\0'; // 手动补\0
                    int n = g_protocol(result->readbuf, result->writebuf);
                    if (n <= 0 || n >= BUFFER_SIZE)
                    {
                        conn_close(result);
                        continue;
                    }
                    result->writelen=n;
                    if(set_event_send(&ring, result)!=0)
                    {
                        conn_close(result);
                    }
                    // printf("read : %s\n",result->buf);
                    // free(result);
                }
                else ////disconnet  || fail
                {
                    conn_close(result);
                    continue;
                    // printf("disconneted now\n");
                    // close(result->fd);
                    // free(result->buf);
                    // free(result);
                }
            }
            else if (result->event == EVENT_WRITE)
            {
                  if(res<=0)
                  {
                    conn_close(result);
                    continue;
                  }
                  else
                  {
                    if(set_event_recv(&ring,result)!=0)
                    {
                        conn_close(result);
                    }
                  }
            }
        }
        io_uring_cq_advance(&ring, nready);
    }
    io_uring_queue_exit(&ring);
    close(sockfd);
    return 0;
}