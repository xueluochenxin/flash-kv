#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

int main(void)
{
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port   = htons(8888);
    inet_pton(AF_INET, "192.168.74.128", &server_addr.sin_addr);

    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        return -1;
    }



    

    
    const char *request2 = "GETB AAA\n";

    /* ── 核心测试逻辑：连续发送，强行制造 TCP 粘包 ── */
    send(sockfd, request2, strlen(request2), 0);



    /* 
     * 接收服务器的回复（一次性读取所有返回）
     * 如果服务器处理粘包失败，这里可能会返回报错信息或者直接断开连接
     */
    char buf[4096] = {0};
    int n = recv(sockfd, buf, sizeof(buf) - 1, 0);
    if (n > 0) {
        printf("收到服务器回复:\n%s\n", buf);
    } else if (n == 0) {
        printf("服务器断开了连接（可能粘包解析失败崩溃了）\n");
    } else {
        perror("recv");
    }

    close(sockfd);
    return 0;
}