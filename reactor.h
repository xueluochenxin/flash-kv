#include <stdio.h>      // printf, perror
#include <stdlib.h>     // exit, malloc
#include <string.h>     // memset, bzero
#include <unistd.h>     // close, read, write
#include <errno.h>      // errno
#include <fcntl.h>      // fcntl (如果要用非阻塞 ET 模式)
#include <sys/socket.h> // socket, bind, listen, accept, recv, send
#include <netinet/in.h> // sockaddr_in, htons, htonl, INADDR_ANY
#include <arpa/inet.h>  // inet_ntop, inet_addr (打印客户端 IP)
#include <sys/epoll.h>  // epoll_create1, epoll_ctl, epoll_wait, struct epoll_event

#define BUFFER_SIZE 1024
#define CONN_SIZE 65536
#define ADD_FLAG 1
#define MOD_FLAG 0
#define MAX_EVENTS 1024

typedef int (*CALLBACK)(int fd);
typedef int (*msg_handler)(char **msg, int length, char *response);


int accept_cb(int fd);
int recv_cb(int fd);
int send_cb(int fd);
int set_nonblocking(int fd);
int set_event(int fd, int event, int flag);
int init_server(unsigned short port);
int main_loop(struct epoll_event *Events, int sockfd);

struct conn
{
    int fd;
    char rbuffer[BUFFER_SIZE];
    int rlen;
    char wbuffer[BUFFER_SIZE];
    int wlen;

    CALLBACK send_cb;
    union
    {
        CALLBACK recv_cb;
        CALLBACK accept_cb;
    } r_action;
};

int epfd = -1;
struct conn conn_list[CONN_SIZE] = {0};

struct kv_array_entry
{
    char* key;
    char* value;
};
