#include "reactor.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#define NETWORK_REACTOR 0
#define NETWORK_PROACTOR 1
#define NETWORK_NTYCO 2
#define NETWORK_SELECT NETWORK_REACTOR
#define ARRAY_LENTH 1024
struct kv_array_entry* kv_array;

void* kv_malloc(int size)
{
    return malloc(size);
}
void kv_free(void* ptr)
{
  return free(ptr);
}
int kv_array_create(struct kv_array_entry** kv_array)
{
*kv_array=malloc(sizeof(struct kv_array_entry)*ARRAY_LENTH);
if(*kv_array==NULL)
{
    return -1;
}
memset(*kv_array,0,sizeof(struct kv_array_entry)*ARRAY_LENTH);
return 1;
}

int kv_array_distory(struct kv_array_entry* kv_array)
{
    for(int i=0;i<ARRAY_LENTH;++i)
    {
        if(kv_array[i].key!=NULL)
        {
            kv_free(kv_array[i].key);
        }
        if(kv_array[i].value!=NULL)
        {
            kv_free(kv_array[i].value);
        }
    }    
kv_free(kv_array);
return 1;
}

int kv_array_set(struct kv_array_entry* kv_array,char **msg,char* output)
{
int index=0;
int issucces=0;
while(index<ARRAY_LENTH)
{
    if(kv_array[index].key==NULL)
    {
        issucces=1;
        break;        
    }
    index++;
}
if(issucces==0)
{
    //printf("array is full\n");
    sprintf(output,"WRONG");
    return -1; 
}
    kv_array[index].key   = kv_malloc(strlen(msg[1]) + 1);  
    kv_array[index].value = kv_malloc(strlen(msg[2]) + 1);
    strcpy(kv_array[index].key, msg[1]);                     
    strcpy(kv_array[index].value, msg[2]);
sprintf(output,"OK");
    return 1;
}

int kv_array_get(struct kv_array_entry* kv_array,char **msg,char* output)
{
int index=0;
int success=0;
while(index<ARRAY_LENTH)
{
    if(kv_array[index].key != NULL &&strcmp(kv_array[index].key,msg[1])==0)
    {
        sprintf(output,"%s",kv_array[index].value);
        success=1;
        break;
    }
    index++;
}
if(success==1)
{
    return 1;
}
if(success==0)
{
    sprintf(output,"INEXIST");
    return -1;
}
}

int kv_array_del(struct kv_array_entry* kv_array,char **msg,char* output)
{
int index=0;
int success=0;
while(index<ARRAY_LENTH)
{
    if(kv_array[index].key != NULL &&strcmp(kv_array[index].key,msg[1])==0)
    {
        sprintf(output,"OK");
        kv_free(kv_array[index].key);
        kv_free(kv_array[index].value);
        kv_array[index].key=NULL;
        kv_array[index].value=NULL;
        success=1;
        break;
    }
    index++;
}
if(success)
{
    return 1;
}
if(success==0)
{
    sprintf(output,"INEXIST");
    return -1;
}
}


int kv_array_mod(struct kv_array_entry* kv_array,char **msg,char* output)
{
int index=0;
int success=0;
while(index<ARRAY_LENTH)
{
    if(kv_array[index].key != NULL &&strcmp(kv_array[index].key,msg[1])==0)
    {
        sprintf(output,"OK");
        kv_free(kv_array[index].value);
        kv_array[index].value = kv_malloc(strlen(msg[2]) + 1);
        strcpy(kv_array[index].value, msg[2]);
        success=1;
        break;
    }
    index++;
}
if(success==1)
{
    return 1;
}
if(success==0)
{
    sprintf(output,"INEXIST");
    return -1;
}
}

int kv_array_exist(struct kv_array_entry* kv_array,char **msg,char* output)
{
int index=0;
int success=0;
while(index<ARRAY_LENTH)
{
    if(kv_array[index].key != NULL &&strcmp(kv_array[index].key,msg[1])==0)
    {
        sprintf(output,"YES");
        success=1;
        break;
    }
    index++;
}
if(success==1)
{
    return 1;
}
if(success==0)
{
    sprintf(output,"NO");
    return -1;
}
}

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
            return -1; // ★ 关键：失败必须 return
        }
        // 把新的fd加入ev监控列表
        set_nonblocking(clientfd);
        conn_list[clientfd].fd = clientfd;
        conn_list[clientfd].r_action.recv_cb = recv_cb;
        conn_list[clientfd].send_cb = send_cb;
        memset(conn_list[clientfd].rbuffer, 0, BUFFER_SIZE);
        conn_list[clientfd].rlen = 0;
        memset(conn_list[clientfd].wbuffer, 0, BUFFER_SIZE);
        conn_list[clientfd].wlen = 0;
        set_event(clientfd, EPOLLIN, ADD_FLAG);
    }
    return 0;
}

int recv_cb(int fd)
{
    memset(conn_list[fd].rbuffer, 0, BUFFER_SIZE);
    int count = recv(fd, conn_list[fd].rbuffer, BUFFER_SIZE - 1, 0);
    if (count == 0) // 断开连接
    {
        //printf("client disconnect:%d \n", fd);
        close(fd);
        epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
        return -1;
    }
    if (count < 0) // 出错
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
        {
            // 读完了，正常结束
            return 0;
        }
        else // 出错了
        {
            perror("recv");
            epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
            close(fd);
            return -1;
        }
    }
    if(count>0)
    {
    // recv不会自动加入\0  需要我们手动加进去
    conn_list[fd].rbuffer[count] = '\0';
    conn_list[fd].rlen = count;
    int resplen=kvs_protocol(conn_list[fd].rbuffer,count,conn_list[fd].wbuffer);
    conn_list[fd].wlen=resplen;
    set_event(fd,EPOLLOUT,MOD_FLAG);
    }
    return count;
}

int send_cb(int fd)
{
  int n=send(fd,conn_list[fd].wbuffer,conn_list[fd].wlen,0);
      if (n <= 0)
    {
        // 发送失败或对端关闭
        epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
        close(fd);
        return -1;
    }
        if (n < conn_list[fd].wlen)
    {
        // 只发了一部分
        memmove(conn_list[fd].wbuffer,
                conn_list[fd].wbuffer + n,
                conn_list[fd].wlen - n);
        conn_list[fd].wlen -= n;
        set_event(fd, EPOLLOUT, MOD_FLAG);   
        return n;
    }
  conn_list[fd].wlen = 0;
  set_event(fd,EPOLLIN,MOD_FLAG);
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
                accept_cb(Events[i].data.fd);
            }
            else
            {
                if (Events[i].events & (EPOLLERR | EPOLLHUP)) //出错  挂断
                {
                    epoll_ctl(epfd, EPOLL_CTL_DEL, Events[i].data.fd, NULL);
                    close(Events[i].data.fd);
                    continue; //  处理完直接跳过，不要再走 IN/OUTS
                }
                if (Events[i].events & EPOLLIN)
                {
                    if (recv_cb(Events[i].data.fd) < 0) continue;   //  已关闭，跳过 OUT
                }
                if (Events[i].events & EPOLLOUT)
                {
                    send_cb(Events[i].data.fd);
                }
            }
        }
    }
    return 0;
}

int reactor_start(int port)
{
    epfd = epoll_create1(0);

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

int split_command(char* input,char* output[]) //返回值为解析出来的单词数目
{
    int count = 0;
    char *p = input;

    while (*p) {
        /* 1. 跳过所有空白 */
        while (*p && isspace((unsigned char)*p))
            p++;

        /* 2. 到达字符串末尾，结束 */
        if (*p == '\0')
            break;

        /* 3. 记录这个单词的开始 */
        output[count++] = p;

        /* 4. 向后扫描直到遇到空白 */
        while (*p!='\0' && !isspace((unsigned char)*p))
            p++;

        /* 5. 把分隔符替换成 '\0'，以便单词独立成串 */
        if (*p) {
            *p = '\0';
            p++;              /* 跳到下一个位置继续找 */
        }
    }

    return count;
}
int kvs_protocol(char* msg,int lenth,char* response)
{
    char *tokens[16]={0};
    int count=split_command(msg,tokens);
        if(count == 0||tokens[0]==NULL)          // 空输入保护
    {
        sprintf(response,"ERROR");
        return -1;
    }
	char* command=tokens[0];

    if(strcmp(command,"SET")==0)
	{
        kv_array_set(kv_array,tokens,response);
	}
    if(strcmp(command,"GET")==0)
	{
       kv_array_get(kv_array,tokens,response);
	}
    if(strcmp(command,"DEL")==0)
	{
		kv_array_del(kv_array,tokens,response);
	}
    if(strcmp(command,"MOD")==0)
	{
		kv_array_mod(kv_array,tokens,response);
	}
	if(strcmp(command,"EXIST")==0)
	{
		kv_array_exist(kv_array,tokens,response);
	}
    return strlen(response);
}
int main(int argc,char** argv)
{
    if (argc != 2) return -1;

	int port = atoi(argv[1]);
    
    kv_array_create(&kv_array);
#if (NETWORK_SELECT == NETWORK_REACTOR)
	reactor_start(port);  
#elif (NETWORK_SELECT == NETWORK_PROACTOR)
	ntyco_start(port, kvs_protocol);
#elif (NETWORK_SELECT == NETWORK_NTYCO)
	proactor_start(port, kvs_protocol);
#endif
    kv_array_distory(kv_array);
}