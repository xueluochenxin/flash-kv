/*
 * test_client.c —— flash_kv 的 C 语言测试客户端
 *
 * 编译：  make test_client
 * 运行：  ./test_client                    （默认 127.0.0.1:8888）
 *         ./test_client 192.168.1.10 9999
 *
 * 特点：
 *   - 不依赖服务器里的任何网络层，只用标准 socket
 *   - 复用项目自己的 kvbuf.c，顺带把 kvbuf 也测一遍
 *   - 客户端自己也要处理"半包/粘包"——因为 TCP 对两边一视同仁
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "kvbuf.h"

/* ══════════════════ 结果统计 ══════════════════ */
static int g_pass = 0;
static int g_fail = 0;

static void report(int ok, const char *fmt, ...)
{
    char    buf[512];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (ok)
    {
        g_pass++;
        printf("  [ OK ] %s\n", buf);
    }
    else
    {
        g_fail++;
        printf("  [FAIL] %s\n", buf);
    }
}

static const char *safe(const kvbuf *b)
{
    return b->p ? b->p : "(无应答)";
}

/* ══════════════════ 连接 ══════════════════ */
static int kv_connect(const char *ip, unsigned short port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
    {
        perror("socket");
        return -1;
    }

    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port   = htons(port);
    if (inet_pton(AF_INET, ip, &a.sin_addr) != 1)
    {
        fprintf(stderr, "IP 格式不对: %s\n", ip);
        close(fd);
        return -1;
    }

    if (connect(fd, (struct sockaddr *)&a, sizeof(a)) < 0)
    {
        perror("connect");
        close(fd);
        return -1;
    }

    /* 加个 5 秒超时：服务器卡住时客户端不要跟着一起卡死 */
    struct timeval tv = {5, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    return fd;
}

/* ══════════════════ 发送：一定要发完 ══════════════════ */
static int send_all(int fd, const char *buf, size_t len)
{
    size_t sent = 0;

    while (sent < len)
    {
        ssize_t n = send(fd, buf + sent, len - sent, 0);

        if (n > 0) { sent += (size_t)n; continue; }
        if (n < 0 && errno == EINTR) continue;

        return -1;
    }
    return 0;
}

/* ══════════════════ 接收：攒够一条 '\n' 结尾的应答 ══════════════════
 * in   : 累积缓冲（跨多次 recv 保存没读完的数据）
 * line : 输出，去掉 '\n' 后以 '\0' 结尾
 * 返回 : 1 成功 / 0 对端关闭 / -1 出错
 */
static int read_line(int fd, kvbuf *in, kvbuf *line)
{
    while (1)
    {
        if (in->len > 0)
        {
            char *nl = memchr(in->p, '\n', in->len);
            if (nl != NULL)
            {
                size_t n = (size_t)(nl - in->p);

                if (kvbuf_reserve(line, n + 1) != 0)
                    return -1;
                memcpy(line->p, in->p, n);
                line->p[n] = '\0';
                line->len  = n;

                /* 把这一条从累积缓冲里摘掉 */
                memmove(in->p, in->p + n + 1, in->len - n - 1);
                in->len -= (n + 1);
                return 1;
            }
        }

        if (kvbuf_reserve(in, in->len + 4096) != 0)
            return -1;

        ssize_t r = recv(fd, in->p + in->len, in->cap - in->len, 0);
        if (r > 0) { in->len += (size_t)r; continue; }
        if (r == 0) return 0;
        if (errno == EINTR) continue;
        return -1;
    }
}

/* 发一条命令并读一条应答。text 必须以 '\n' 结尾 */
static int cmd(int fd, kvbuf *in, kvbuf *line, const char *text)
{
    size_t n = strlen(text);

    if (send_all(fd, text, n) != 0)
        return -1;
    return read_line(fd, in, line);
}

/* ══════════════════ 测试 1：基本命令 ══════════════════ */
static void test_basic(const char *ip, unsigned short port)
{
    printf("\n[1] 基本命令\n");

    int fd = kv_connect(ip, port);
    if (fd < 0) { report(0, "连接服务器"); return; }

    kvbuf in, line;
    kvbuf_init(&in);
    kvbuf_init(&line);

    cmd(fd, &in, &line, "SET a 1\n");
    report(strcmp(safe(&line), "OK") == 0, "SET a 1 -> %s", safe(&line));

    cmd(fd, &in, &line, "GET a\n");
    report(strcmp(safe(&line), "1") == 0, "GET a -> %s", safe(&line));

    cmd(fd, &in, &line, "EXIST a\n");
    report(strcmp(safe(&line), "YES") == 0, "EXIST a -> %s", safe(&line));

    cmd(fd, &in, &line, "MOD a 22\n");
    report(strcmp(safe(&line), "OK") == 0, "MOD a 22 -> %s", safe(&line));

    cmd(fd, &in, &line, "GET a\n");
    report(strcmp(safe(&line), "22") == 0, "GET a -> %s (MOD 生效)", safe(&line));

    cmd(fd, &in, &line, "DEL a\n");
    report(strcmp(safe(&line), "OK") == 0, "DEL a -> %s", safe(&line));

    cmd(fd, &in, &line, "GET a\n");
    report(strcmp(safe(&line), "INEXIST") == 0, "GET a -> %s", safe(&line));

    cmd(fd, &in, &line, "SET onlykey\n");
    report(strcmp(safe(&line), "INVALID") == 0, "缺参数 SET onlykey -> %s", safe(&line));

    kvbuf_destroy(&in);
    kvbuf_destroy(&line);
    close(fd);
}

/* ══════════════════ 测试 2：一次发多条（批量 / 粘包） ══════════════════ */
static void test_batch(const char *ip, unsigned short port)
{
    printf("\n[2] 一次发 100 条（批量 / 粘包）\n");

    int fd = kv_connect(ip, port);
    if (fd < 0) { report(0, "连接服务器"); return; }

    kvbuf in, line, tx;
    kvbuf_init(&in);
    kvbuf_init(&line);
    kvbuf_init(&tx);

    for (int i = 0; i < 100; i++)
    {
        char tmp[64];
        int  n = snprintf(tmp, sizeof(tmp), "SET k%d v%d\n", i, i);
        kvbuf_append(&tx, tmp, (size_t)n);
    }

    if (send_all(fd, tx.p, tx.len) != 0)
    {
        report(0, "发送 100 条");
        goto out;
    }

    int good = 1;
    for (int i = 0; i < 100; i++)
    {
        if (read_line(fd, &in, &line) != 1 || strcmp(safe(&line), "OK") != 0)
        {
            good = 0;
            break;
        }
    }
    report(good, "收到 100 个 OK");

    int data_ok = 1;
    for (int i = 0; i < 100; i++)
    {
        char want[32], tmp[64];

        snprintf(tmp,  sizeof(tmp),  "GET k%d\n", i);
        snprintf(want, sizeof(want), "v%d", i);

        if (cmd(fd, &in, &line, tmp) != 1 || strcmp(safe(&line), want) != 0)
        {
            data_ok = 0;
            break;
        }
    }
    report(data_ok, "100 个 key 的值全部正确");

out:
    kvbuf_destroy(&in);
    kvbuf_destroy(&line);
    kvbuf_destroy(&tx);
    close(fd);
}

/* ══════════════════ 测试 3：半包（一条命令分两次发） ══════════════════ */
static void test_split(const char *ip, unsigned short port)
{
    printf("\n[3] 一条命令分两次发（半包）\n");

    int fd = kv_connect(ip, port);
    if (fd < 0) { report(0, "连接服务器"); return; }

    kvbuf in, line;
    kvbuf_init(&in);
    kvbuf_init(&line);

    send_all(fd, "SET hw spl", 10);
    usleep(200 * 1000);
    send_all(fd, "it value\n", 9);

    if (read_line(fd, &in, &line) != 1)
    {
        report(0, "半包 SET");
        goto out;
    }
    report(strcmp(safe(&line), "OK") == 0, "分两次发的 SET 被拼成一条 -> %s", safe(&line));

    cmd(fd, &in, &line, "GET hw\n");
    report(strcmp(safe(&line), "split value") == 0, "值带空格且完整 -> \"%s\"", safe(&line));

out:
    kvbuf_destroy(&in);
    kvbuf_destroy(&line);
    close(fd);
}

/* ══════════════════ 测试 4：大 value ══════════════════ */
static void test_big_value(const char *ip, unsigned short port)
{
    const size_t N = 100 * 1024;      /* 100 KB */

    printf("\n[4] 100KB 大 value\n");

    char *val = malloc(N + 1);
    if (val == NULL) { report(0, "malloc"); return; }
    memset(val, 'A', N);
    val[N] = '\0';

    int fd = kv_connect(ip, port);
    if (fd < 0) { free(val); report(0, "连接服务器"); return; }

    kvbuf in, line, tx;
    kvbuf_init(&in);
    kvbuf_init(&line);
    kvbuf_init(&tx);

    /* 拼 "SET big <100KB>\n" */
    kvbuf_append(&tx, "SET big ", 8);
    kvbuf_append(&tx, val, N);
    kvbuf_append(&tx, "\n", 1);

    if (send_all(fd, tx.p, tx.len) != 0 || read_line(fd, &in, &line) != 1)
    {
        report(0, "大 value SET");
        goto out;
    }
    report(strcmp(safe(&line), "OK") == 0, "SET 100KB -> %s", safe(&line));

    if (cmd(fd, &in, &line, "GET big\n") != 1)
    {
        report(0, "大 value GET");
        goto out;
    }
    report(line.len == N && memcmp(line.p, val, N) == 0,
           "GET 100KB 取回 %zu 字节，内容一致", line.len);

out:
    kvbuf_destroy(&in);
    kvbuf_destroy(&line);
    kvbuf_destroy(&tx);
    close(fd);
    free(val);
}

/* ══════════════════ 测试 5：跨连接 ══════════════════ */
static void test_cross_conn(const char *ip, unsigned short port)
{
    printf("\n[5] 跨连接：A 存，B 取\n");

    kvbuf in, line;
    kvbuf_init(&in);
    kvbuf_init(&line);

    int a = kv_connect(ip, port);
    if (a < 0) { report(0, "连接 A"); return; }
    cmd(a, &in, &line, "SET cross 42\n");
    report(strcmp(safe(&line), "OK") == 0, "A 存 -> %s", safe(&line));
    close(a);                          /* A 断开 */

    usleep(50 * 1000);

    int b = kv_connect(ip, port);      /* B 是全新连接 */
    if (b < 0) { report(0, "连接 B"); kvbuf_destroy(&in); kvbuf_destroy(&line); return; }

    cmd(b, &in, &line, "GET cross\n");
    report(strcmp(safe(&line), "42") == 0, "B 取 -> %s", safe(&line));

    close(b);
    kvbuf_destroy(&in);
    kvbuf_destroy(&line);
}

/* ══════════════════ 测试 6：长连接上连续发 ══════════════════ */
static void test_long_conn(const char *ip, unsigned short port)
{
    printf("\n[6] 同一条连接上连发 500 组 SET+GET\n");

    int fd = kv_connect(ip, port);
    if (fd < 0) { report(0, "连接服务器"); return; }

    kvbuf in, line;
    kvbuf_init(&in);
    kvbuf_init(&line);

    int good = 1;
    for (int i = 0; i < 500; i++)
    {
        char cmdtext[64], want[32];

        snprintf(cmdtext, sizeof(cmdtext), "SET ln %d\n", i);
        if (cmd(fd, &in, &line, cmdtext) != 1 || strcmp(safe(&line), "OK") != 0)
        {
            printf("       第 %d 轮 SET 失败: %s\n", i, safe(&line));
            good = 0;
            break;
        }

        snprintf(cmdtext, sizeof(cmdtext), "GET ln\n");
        snprintf(want, sizeof(want), "%d", i);
        if (cmd(fd, &in, &line, cmdtext) != 1 || strcmp(safe(&line), want) != 0)
        {
            printf("       第 %d 轮 GET 失败: 期望 %s 实际 %s\n", i, want, safe(&line));
            good = 0;
            break;
        }
    }
    report(good, "500 组读写全部正确");

    kvbuf_destroy(&in);
    kvbuf_destroy(&line);
    close(fd);
}

/* ══════════════════ main ══════════════════ */
int main(int argc, char **argv)
{
    const char   *ip   = (argc > 1) ? argv[1] : "127.0.0.1";
    unsigned short port = (argc > 2) ? (unsigned short)atoi(argv[2]) : 8888;

    printf("======== flash_kv 客户端测试 ========\n");
    printf("目标: %s:%u\n", ip, port);

    test_basic      (ip, port);
    test_batch      (ip, port);
    test_split      (ip, port);
    test_big_value  (ip, port);
    test_cross_conn (ip, port);
    test_long_conn  (ip, port);

    printf("\n=====================================\n");
    printf("通过 %d 项，失败 %d 项\n", g_pass, g_fail);
    printf("=====================================\n");

    return g_fail == 0 ? 0 : 1;
}
