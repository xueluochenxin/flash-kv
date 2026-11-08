#include "reactor.h"


int epfd = -1;
struct conn conn_list[CONN_SIZE] = {0};
PROTOCOL g_protocol = NULL;



int set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1)
        return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int set_event(int fd, int event, int flag) // 给定fd和event 加入epoll检测目标当中.
{
    struct epoll_event ev;
    ev.events = event;
    ev.data.fd = fd;
    if (flag)
    {
        return epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);
    }
    else
    {
        return epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev);
    }
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
static void conn_close(int fd)
{
    kvbuf_destroy(&conn_list[fd].rbuf);     /* ← 漏了这行就泄漏 + 脏数据 */
    kvbuf_destroy(&conn_list[fd].sbuf);
    epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
    close(fd);
}
int accept_cb(int fd)
{
    while (1)
    {
        struct sockaddr_in addr;
        socklen_t socklen = sizeof(struct sockaddr_in);
        int clientfd = accept(fd, (struct sockaddr *)&addr, &socklen);
        if (clientfd < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break; // 没更多连接
            if (errno == EINTR)
                continue;
            perror("accept");
            return -1; //  关键：失败必须 return
        }
        // 把新的fd加入ev监控列表
        set_nonblocking(clientfd);
        conn_list[clientfd].fd = clientfd;
        conn_list[clientfd].r_action.recv_cb = recv_cb;
        conn_list[clientfd].send_cb = send_cb;
        kvbuf_init(&conn_list[clientfd].rbuf);
        kvbuf_init(&conn_list[clientfd].sbuf);
        set_event(clientfd, EPOLLIN, ADD_FLAG);
    }
    return 0;
}

int recv_cb(int fd)
{
    struct conn *c = &conn_list[fd];

    /* ── 1. 一次把内核里的数据全读光 ── */
    while (1) {
        if (kvbuf_reserve(&c->rbuf, c->rbuf.len + BUFFER_SIZE) != 0) {
            conn_close(fd);
            return -1;
        }

        int n = recv(fd, c->rbuf.p + c->rbuf.len,
                     c->rbuf.cap - c->rbuf.len, 0);

        if (n > 0) { c->rbuf.len += (size_t)n; continue; }
        if (n == 0) { conn_close(fd); return -1; }        /* 对端关闭 */
        if (errno == EAGAIN || errno == EWOULDBLOCK) break;  //没数据了
        if (errno == EINTR) continue;
        conn_close(fd);
        return -1;
    }

    /* ── 2. 切分 + 处理 ── */
    if (kvbuf_process(&c->rbuf, &c->sbuf, g_protocol) != 0) {
        conn_close(fd);                                   
        return -1;
    }

    /* ── 3. 有应答就切到可写 ── */
    if (c->sbuf.len > 0)
        set_event(fd, EPOLLOUT, MOD_FLAG);

    return 1;
}

int send_cb(int fd)// 返回值小于0表示send失败
{  
    struct conn *c = &conn_list[fd];

    if (c->sbuf.len == 0) {                 /* 没东西发，回去收 */
        set_event(fd, EPOLLIN, MOD_FLAG);
        return 0;
    }

    int n = send(fd, c->sbuf.p, c->sbuf.len, 0);
    if (n <= 0) { conn_close(fd); return -1; }

    if ((size_t)n < c->sbuf.len) {          /* 只发了一部分 */
        memmove(c->sbuf.p, c->sbuf.p + n, c->sbuf.len - (size_t)n);
        c->sbuf.len -= (size_t)n;
        set_event(fd, EPOLLOUT, MOD_FLAG);  /* 继续等可写 */
        return n;
    }

    c->sbuf.len = 0;                        /* 全发完了 */
    set_event(fd, EPOLLIN, MOD_FLAG);       /* 回去收数据 */
    return n;
}

int main_loop(struct epoll_event *Events, int sockfd)
{
    while (1)
    {
        int nready = epoll_wait(epfd, Events, MAX_EVENTS, -1);
        if (nready == -1)
        {
            perror("epoll_wait");
            break;
        }
        for (int i = 0; i < nready; ++i)
        {
            if (Events[i].data.fd == sockfd)
            {
                int res=conn_list[sockfd].r_action.accept_cb(sockfd);
                //res< 0 fail
            }
            else
            {
                if (Events[i].events & (EPOLLERR | EPOLLHUP)) //出错  挂断
                {
                    conn_close(Events[i].data.fd);
                    continue; //  处理完直接跳过，不要再走 IN/OUTS
                }
                if (Events[i].events & EPOLLIN)
                {
                    if (conn_list[Events[i].data.fd].r_action.recv_cb(Events[i].data.fd) < 0)
                     continue;   //  已关闭，跳过 OUT
                }
                if (Events[i].events & EPOLLOUT)
                {
                    int res=conn_list[Events[i].data.fd].send_cb(Events[i].data.fd);
                    // if res<0   fail
                }
            }
        }
    }
    return 0;
}

int reactor_start(int port,PROTOCOL handler)
{
    epfd = epoll_create1(0);

    g_protocol=handler;

    struct epoll_event Events[MAX_EVENTS];
    memset(Events, 0, sizeof(struct epoll_event) * MAX_EVENTS);

    int sockfd = init_server((unsigned short)port);
    conn_list[sockfd].fd = sockfd;
    conn_list[sockfd].r_action.accept_cb = accept_cb;
    set_event(sockfd, EPOLLIN, ADD_FLAG);

    main_loop(Events, sockfd);
    close(epfd);
    close(sockfd);
    return 0;
}