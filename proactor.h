#ifndef PROACTOR_H
#define PROACTOR_H

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
#include <liburing.h>


#define ENTRIES_LENGTH 1024

#define EVENT_ACCEPT 0
#define EVENT_READ  1
#define EVENT_WRITE 2

#define BUFFER_SIZE 1024
#define CONN_SIZE 1024

typedef int (*PROTOCOL) (char* msg,char* response);

struct conn_info{
int fd;
char readbuf[BUFFER_SIZE];
int readlen;
char writebuf[BUFFER_SIZE];
int writelen;
int event;

};

int set_event_send(struct io_uring *ring, struct conn_info *info);
int set_nonblocking(int fd);
int init_server(unsigned short port); 
int set_event_accept(struct io_uring *ring,int sockfd, struct sockaddr *addr,
                  socklen_t * addrlen,int flags);           
int set_event_recv(struct io_uring *ring,struct conn_info *info);
struct io_uring_sqe *get_sqe(struct io_uring *ring);

void conn_close(struct conn_info *info);
int proactor_start(int port, PROTOCOL handler);

#endif