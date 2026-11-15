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

    const char *msg = 
        "计算机网络把世界上成千上万台设备连接在一起。\n"
        "我们每天使用的网页、聊天、视频，背后都依赖可靠的数据传输。\n"
        "套接字是操作系统提供给应用程序的通信窗口。\n"
        "通过它，程序可以像读写文件一样收发字节流。\n"
        "理解连接建立、数据发送与接收的过程，是写好网络程序的第一步。\n"
        "很多人一开始觉得协议复杂难懂。\n"
        "其实只要动手写一个最简单的客户端，把字节按约定发出去，就能看到它工作。\n"
        "从一条连接开始，慢慢加上长度、校验和分片处理。\n"
        "这个过程会让抽象的协议变得具体而清晰。\n"
        "愿每一个初学者都能在真实的字节流动中找到乐趣。\n"
        "字节本身没有含义，是双方约定的格式赋予它意义。\n"
        "协议就是这套约定的完整说明书，规定每个字节该怎么解释。\n"
        "长度字段告诉接收方要读多少字节，避免把多条消息粘在一起。\n"
        "当数据超过一次能发送的量，就要考虑分片和循环写入。\n"
        "写测试时，用一段较长且内容已知的文本，能很快暴露长度计算的问题。\n"
        "只要接收端返回的内容和原文一字不差，就说明协议实现正确。\n"
        "真实的网络还会出现延迟、丢包和重连，测试应当逐步覆盖。\n"
        "但无论如何，从一段固定文本开始总是稳妥的做法。\n"
        "希望这份示例能帮你顺利跑通第一个收发流程。\n"
        "接下来，就可以在此基础上继续扩展自己的协议了。";

    int len = strlen(msg);
    
    // 修复1：分配足够大的数组。8192足够装下头和正文了
    char request1[8192]; 
    
    // 修复2：去掉 & 符号，传入正确的值
    sprintf(request1, "SETB AAA %d\n%s", len, msg);
    
    const char *request2 = "GETB AAA\n";
    char* request3="SAVE\n";
    /* ── 核心测试逻辑：连续发送，强行制造 TCP 粘包 ── */
    send(sockfd, request1, strlen(request1), 0);
    send(sockfd, request2, strlen(request2), 0);
    send(sockfd,request3,strlen(request3),0);
    printf("发送完毕，request1 长度: %zu, request2 长度: %zu\n", strlen(request1), strlen(request2));

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