/*
 * flash_kv_test_client.c —— flash_kv 独立测试客户端（单文件、零依赖）
 *
 * 编译：
 *     gcc -std=gnu11 -O2 -Wall -Wextra flash_kv_test_client.c -o kv_test
 *
 * 运行：
 *     ./kv_test                       # 默认 127.0.0.1:8888
 *     ./kv_test 192.168.74.128 8888   # 指定 IP 和端口
 *
 * 特点：
 *   - 不依赖服务器代码（不用 kvbuf.c），拷到任何 Linux 机器都能编
 *   - 客户端自己做分帧：TCP 对两端一视同仁，一次 recv 不保证拿到一整条应答
 *   - 所有 socket 操作都有 5 秒超时，服务器卡住时不会把测试挂死
 *   - 返回码：全部通过 0，有失败 1（可以直接用在 shell 的 && 判断里）
 *
 * 覆盖的协议：
 *     SET  <key> <value>   -> OK / INVALID / WRONG
 *     GET  <key>           -> <value> / INEXIST
 *     DEL  <key>           -> OK / INEXIST
 *     MOD  <key> <value>   -> OK / INEXIST
 *     EXIST <key>          -> YES / NO
 *     未知命令 / 参数不足    -> INVALID
 *     空行                  -> ERROR
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* ════════════════════════════════════════════════════════════
 *  可调参数
 * ════════════════════════════════════════════════════════════ */
#define IO_TIMEOUT_SEC   5          /* 单次 recv/send 的超时 */
#define BIG_VALUE_SIZE   (256 * 1024)   /* 大 value 测试用的字节数 */
#define BATCH_COUNT      100        /* 批量测试一次发多少条 */
#define LONG_ROUNDS      500        /* 长连接测试的轮数 */
#define MULTI_CONNS      4          /* 多连接交错测试的连接数 */

/* ════════════════════════════════════════════════════════════
 *  结果统计
 * ════════════════════════════════════════════════════════════ */
static int g_pass = 0;
static int g_fail = 0;

static void report(int ok, const char *fmt, ...)
{
    char    buf[600];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (ok) { g_pass++; printf("  [ OK ] %s\n", buf); }
    else    { g_fail++; printf("  [FAIL] %s\n", buf); }
}

/* 避免打印 NULL */
static const char *S(const char *s) { return s ? s : "(无应答)"; }

static void sleep_ms(long ms)
{
    struct timespec ts;
    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

/* ════════════════════════════════════════════════════════════
 *  一个最小可增长字节缓冲（客户端自己的，跟服务器无关）
 * ════════════════════════════════════════════════════════════ */
typedef struct {
    char  *p;
    size_t len;
    size_t cap;
} buf_t;

static int buf_reserve(buf_t *b, size_t need)
{
    if (need <= b->cap)
        return 0;

    size_t ncap = b->cap ? b->cap * 2 : 4096;
    if (ncap < need)
        ncap = need;

    char *np = realloc(b->p, ncap);
    if (np == NULL)
        return -1;

    b->p   = np;
    b->cap = ncap;
    return 0;
}

static int buf_append(buf_t *b, const void *data, size_t n)
{
    if (n == 0)
        return 0;
    if (b->len + n < b->len)
        return -1;
    if (buf_reserve(b, b->len + n) != 0)
        return -1;

    memcpy(b->p + b->len, data, n);
    b->len += n;
    return 0;
}

static void buf_free(buf_t *b)
{
    free(b->p);
    b->p   = NULL;
    b->len = 0;
    b->cap = 0;
}

/* ════════════════════════════════════════════════════════════
 *  连接 / 发送 / 接收
 * ════════════════════════════════════════════════════════════ */
static int kv_connect(const char *ip, unsigned short port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return -1; }

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
        fprintf(stderr, "connect %s:%u 失败: %s\n", ip, port, strerror(errno));
        close(fd);
        return -1;
    }

    struct timeval tv = {IO_TIMEOUT_SEC, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    return fd;
}

/* send 可能只发出去一部分，必须循环 */
static int send_all(int fd, const void *data, size_t len)
{
    const char *p    = (const char *)data;
    size_t      sent = 0;

    while (sent < len)
    {
        ssize_t n = send(fd, p + sent, len - sent, 0);
        if (n > 0) { sent += (size_t)n; continue; }
        if (n < 0 && errno == EINTR) continue;
        return -1;
    }
    return 0;
}

/*
 * 读一条以 '\n' 结尾的应答。
 *   in   : 累积缓冲，跨多次 recv 保存还没凑成整行的字节
 *   line : 输出，去掉 '\n'，以 '\0' 结尾
 * 返回   : 1 成功 / 0 对端关闭 / -1 出错（含超时）
 */
static int read_line(int fd, buf_t *in, buf_t *line)
{
    for (;;)
    {
        if (in->len > 0)
        {
            char *nl = memchr(in->p, '\n', in->len);
            if (nl != NULL)
            {
                size_t n = (size_t)(nl - in->p);

                if (buf_reserve(line, n + 1) != 0)
                    return -1;

                memcpy(line->p, in->p, n);
                line->p[n] = '\0';
                line->len  = n;

                /* 把这一条从累积缓冲里摘掉，剩下的留给下次 */
                memmove(in->p, in->p + n + 1, in->len - n - 1);
                in->len -= (n + 1);
                return 1;
            }
        }

        if (buf_reserve(in, in->len + 8192) != 0)
            return -1;

        ssize_t r = recv(fd, in->p + in->len, in->cap - in->len, 0);

        if (r > 0) { in->len += (size_t)r; continue; }
        if (r == 0) return 0;
        if (errno == EINTR) continue;
        return -1;
    }
}

/* 原样发送一条命令（text 必须自带结尾 '\n'），再读一条应答 */
static int kv_send_cmd(int fd, buf_t *in, buf_t *line, const char *text)
{
    if (send_all(fd, text, strlen(text)) != 0)
        return -1;
    return read_line(fd, in, line);
}

/* 格式化后发送，例如 kv_send_fmt(fd,&in,&line,"SET %s %s\n", k, v) */
static int kv_send_fmt(int fd, buf_t *in, buf_t *line, const char *fmt, ...)
{
    char    tmp[1024];
    va_list ap;
    int     n;

    va_start(ap, fmt);
    n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);

    if (n < 0 || (size_t)n >= sizeof(tmp))
        return -1;

    if (send_all(fd, tmp, (size_t)n) != 0)
        return -1;
    return read_line(fd, in, line);
}

/* ════════════════════════════════════════════════════════════
 *  测试 1：基本命令
 * ════════════════════════════════════════════════════════════ */
static void test_basic(const char *ip, unsigned short port)
{
    printf("\n[1] 基本命令\n");

    int fd = kv_connect(ip, port);
    if (fd < 0) { report(0, "连接 %s:%u", ip, port); return; }

    buf_t in = {0}, line = {0};

    kv_send_cmd(fd, &in, &line, "SET a 1\n");
    report(line.p && strcmp(line.p, "OK") == 0, "SET a 1 -> %s", S(line.p));

    kv_send_cmd(fd, &in, &line, "GET a\n");
    report(line.p && strcmp(line.p, "1") == 0, "GET a -> %s", S(line.p));

    kv_send_cmd(fd, &in, &line, "EXIST a\n");
    report(line.p && strcmp(line.p, "YES") == 0, "EXIST a -> %s", S(line.p));

    kv_send_cmd(fd, &in, &line, "MOD a 22\n");
    report(line.p && strcmp(line.p, "OK") == 0, "MOD a 22 -> %s", S(line.p));

    kv_send_cmd(fd, &in, &line, "GET a\n");
    report(line.p && strcmp(line.p, "22") == 0, "GET a (MOD 后) -> %s", S(line.p));

    kv_send_cmd(fd, &in, &line, "SET a 33\n");
    report(line.p && strcmp(line.p, "OK") == 0, "重复 SET a 33 -> %s", S(line.p));

    kv_send_cmd(fd, &in, &line, "GET a\n");
    report(line.p && strcmp(line.p, "33") == 0, "GET a (覆盖后) -> %s", S(line.p));

    kv_send_cmd(fd, &in, &line, "DEL a\n");
    report(line.p && strcmp(line.p, "OK") == 0, "DEL a -> %s", S(line.p));

    kv_send_cmd(fd, &in, &line, "GET a\n");
    report(line.p && strcmp(line.p, "INEXIST") == 0, "GET a (已删) -> %s", S(line.p));

    kv_send_cmd(fd, &in, &line, "DEL a\n");
    report(line.p && strcmp(line.p, "INEXIST") == 0, "重复 DEL a -> %s", S(line.p));

    kv_send_cmd(fd, &in, &line, "MOD nokey 1\n");
    report(line.p && strcmp(line.p, "INEXIST") == 0, "MOD 不存在的 key -> %s", S(line.p));

    kv_send_cmd(fd, &in, &line, "EXIST nokey\n");
    report(line.p && strcmp(line.p, "NO") == 0, "EXIST 不存在的 key -> %s", S(line.p));

    buf_free(&in);
    buf_free(&line);
    close(fd);
}

/* ════════════════════════════════════════════════════════════
 *  测试 2：非法输入 —— 服务器不能崩，连接不能断
 * ════════════════════════════════════════════════════════════ */
static void test_invalid(const char *ip, unsigned short port)
{
    printf("\n[2] 非法输入（服务器应回 INVALID/ERROR，且连接不断）\n");

    int fd = kv_connect(ip, port);
    if (fd < 0) { report(0, "连接 %s:%u", ip, port); return; }

    buf_t in = {0}, line = {0};

    static const char *cases[] = {
        "SET onlykey",       /* 缺 value */
        "SET",               /* 只有一个词 */
        "GET",               /* 缺 key */
        "DEL",
        "EXIST",
        "MOD onlykey",       /* 缺 value */
        "HELLO",             /* 未知命令 */
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        char cmd[64];
        snprintf(cmd, sizeof(cmd), "%s\n", cases[i]);

        int r = kv_send_cmd(fd, &in, &line, cmd);
        report(r == 1 && line.p && strcmp(line.p, "INVALID") == 0,
               "%-14s -> %s", cases[i], S(line.p));
    }

    /* 全是空白的行：应回 ERROR，且不能断开连接 */
    {
        int r = kv_send_cmd(fd, &in, &line, "   \n");
        report(r == 1 && line.p && strcmp(line.p, "ERROR") == 0,
               "%-14s -> %s", "(三个空格的行)", S(line.p));
    }

    /* 关键：前面发了这么多非法命令，连接必须还活着 */
    kv_send_cmd(fd, &in, &line, "SET alive 1\n");
    report(line.p && strcmp(line.p, "OK") == 0,
           "经历一堆非法输入后，连接仍然可用 -> %s", S(line.p));

    buf_free(&in);
    buf_free(&line);
    close(fd);
}

/* ════════════════════════════════════════════════════════════
 *  测试 3：value 里带空格 / 特殊字符
 * ════════════════════════════════════════════════════════════ */
static void test_spaces(const char *ip, unsigned short port)
{
    printf("\n[3] value 含空格与特殊字符\n");

    int fd = kv_connect(ip, port);
    if (fd < 0) { report(0, "连接 %s:%u", ip, port); return; }

    buf_t in = {0}, line = {0};

    kv_send_cmd(fd, &in, &line, "SET msg hello world\n");
    report(line.p && strcmp(line.p, "OK") == 0, "SET msg hello world -> %s", S(line.p));

    kv_send_cmd(fd, &in, &line, "GET msg\n");
    report(line.p && strcmp(line.p, "hello world") == 0, "GET msg -> \"%s\"", S(line.p));

    kv_send_cmd(fd, &in, &line, "SET m2 a   b   c\n");
    report(line.p && strcmp(line.p, "OK") == 0, "SET m2 a   b   c -> %s", S(line.p));

    kv_send_cmd(fd, &in, &line, "GET m2\n");
    report(line.p && strcmp(line.p, "a   b   c") == 0, "GET m2 (中间多个空格应保留) -> \"%s\"", S(line.p));

    kv_send_cmd(fd, &in, &line, "SET m3    trimmed   \n");
    report(line.p && strcmp(line.p, "OK") == 0, "SET m3 前后带空白 -> %s", S(line.p));

    kv_send_cmd(fd, &in, &line, "GET m3\n");
    report(line.p && strcmp(line.p, "trimmed") == 0, "GET m3 (前后空白应被裁掉) -> \"%s\"", S(line.p));

    kv_send_cmd(fd, &in, &line, "SET json {\"name\": \"kv\", \"list\": [1, 2, 3]}\n");
    report(line.p && strcmp(line.p, "OK") == 0, "SET 一个 JSON -> %s", S(line.p));

    kv_send_cmd(fd, &in, &line, "GET json\n");
    report(line.p && strcmp(line.p, "{\"name\": \"kv\", \"list\": [1, 2, 3]}") == 0,
           "GET json 原样返回 -> %s", S(line.p));

    buf_free(&in);
    buf_free(&line);
    close(fd);
}

/* ════════════════════════════════════════════════════════════
 *  测试 4：一次发多条（批量 / 粘包）
 * ════════════════════════════════════════════════════════════ */
static void test_batch(const char *ip, unsigned short port)
{
    printf("\n[4] 一次发 %d 条命令（批量 / 粘包）\n", BATCH_COUNT);

    int fd = kv_connect(ip, port);
    if (fd < 0) { report(0, "连接 %s:%u", ip, port); return; }

    buf_t in = {0}, line = {0}, tx = {0};

    for (int i = 0; i < BATCH_COUNT; i++)
    {
        char tmp[64];
        int  n = snprintf(tmp, sizeof(tmp), "SET b%d v%d\n", i, i);
        buf_append(&tx, tmp, (size_t)n);
    }

    if (send_all(fd, tx.p, tx.len) != 0)
    {
        report(0, "发送 %d 条命令", BATCH_COUNT);
        goto done;
    }

    int ok_count = 0;
    for (int i = 0; i < BATCH_COUNT; i++)
    {
        if (read_line(fd, &in, &line) != 1)
            break;
        if (line.p && strcmp(line.p, "OK") == 0)
            ok_count++;
    }
    report(ok_count == BATCH_COUNT, "收到 %d/%d 个 OK", ok_count, BATCH_COUNT);

    int data_ok = 0;
    for (int i = 0; i < BATCH_COUNT; i++)
    {
        char want[32], cmd[64];

        snprintf(cmd,  sizeof(cmd),  "GET b%d\n", i);
        snprintf(want, sizeof(want), "v%d", i);

        if (kv_send_cmd(fd, &in, &line, cmd) != 1)
            break;
        if (line.p == NULL || strcmp(line.p, want) != 0)
            break;
        data_ok++;
    }
    report(data_ok == BATCH_COUNT, "%d/%d 个 key 的值都正确", data_ok, BATCH_COUNT);

done:
    buf_free(&in);
    buf_free(&line);
    buf_free(&tx);
    close(fd);
}

/* ════════════════════════════════════════════════════════════
 *  测试 5：一条命令分多次发（半包）
 * ════════════════════════════════════════════════════════════ */
static void test_split(const char *ip, unsigned short port)
{
    printf("\n[5] 一条命令拆成 3 次发（半包）\n");

    int fd = kv_connect(ip, port);
    if (fd < 0) { report(0, "连接 %s:%u", ip, port); return; }

    buf_t in = {0}, line = {0};

    /* 一条完整命令 "SET hw hello world\n" 拆成三段发出去 */
    static const char *part[] = { "SET hw hel", "lo wor", "ld\n" };

    for (size_t i = 0; i < sizeof(part) / sizeof(part[0]); i++)
    {
        send_all(fd, part[i], strlen(part[i]));
        sleep_ms(150);
    }

    int r = read_line(fd, &in, &line);
    report(r == 1 && line.p && strcmp(line.p, "OK") == 0,
           "三次发完才凑成一条 -> %s", S(line.p));

    kv_send_cmd(fd, &in, &line, "GET hw\n");
    report(line.p && strcmp(line.p, "hello world") == 0, "GET hw -> \"%s\"", S(line.p));

    buf_free(&in);
    buf_free(&line);
    close(fd);
}

/* ════════════════════════════════════════════════════════════
 *  测试 6：大 value（跨多次 recv，逐字节比对）
 * ════════════════════════════════════════════════════════════ */
static void test_big_value(const char *ip, unsigned short port)
{
    const size_t N = BIG_VALUE_SIZE;

    printf("\n[6] 大 value（%zu KB，逐字节比对）\n", N / 1024);

    char *val = malloc(N + 1);
    if (val == NULL) { report(0, "malloc 失败"); return; }

    /* 用有规律的内容，任何错位/截断都能被发现 */
    for (size_t i = 0; i < N; i++)
        val[i] = (char)('a' + (i % 26));
    val[N] = '\0';

    int fd = kv_connect(ip, port);
    if (fd < 0) { free(val); report(0, "连接 %s:%u", ip, port); return; }

    buf_t in = {0}, line = {0}, tx = {0};

    buf_append(&tx, "SET big ", 8);
    buf_append(&tx, val, N);
    buf_append(&tx, "\n", 1);

    if (send_all(fd, tx.p, tx.len) != 0 || read_line(fd, &in, &line) != 1)
    {
        report(0, "发送大 value");
        goto done;
    }
    report(line.p && strcmp(line.p, "OK") == 0, "SET %zu KB -> %s", N / 1024, S(line.p));

    if (kv_send_cmd(fd, &in, &line, "GET big\n") != 1)
    {
        report(0, "取回大 value");
        goto done;
    }

    report(line.len == N, "取回长度 %zu / 期望 %zu", line.len, N);
    report(line.len == N && memcmp(line.p, val, N) == 0, "内容逐字节一致");

    kv_send_cmd(fd, &in, &line, "EXIST big\n");
    report(line.p && strcmp(line.p, "YES") == 0, "EXIST big -> %s", S(line.p));

    kv_send_cmd(fd, &in, &line, "DEL big\n");
    report(line.p && strcmp(line.p, "OK") == 0, "DEL big -> %s", S(line.p));

done:
    buf_free(&in);
    buf_free(&line);
    buf_free(&tx);
    close(fd);
    free(val);
}

/* ════════════════════════════════════════════════════════════
 *  测试 7：跨连接（A 存，B 取）—— 能抓出"状态没清干净"
 * ════════════════════════════════════════════════════════════ */
static void test_cross_conn(const char *ip, unsigned short port)
{
    printf("\n[7] 跨连接：A 存完就断开，B 是全新连接\n");

    buf_t in = {0}, line = {0};

    int a = kv_connect(ip, port);
    if (a < 0) { report(0, "连接 A"); return; }

    kv_send_cmd(a, &in, &line, "SET cross 42\n");
    report(line.p && strcmp(line.p, "OK") == 0, "A 存 -> %s", S(line.p));
    close(a);

    sleep_ms(50);

    int b = kv_connect(ip, port);
    if (b < 0) { report(0, "连接 B"); buf_free(&in); buf_free(&line); return; }

    kv_send_cmd(b, &in, &line, "GET cross\n");
    report(line.p && strcmp(line.p, "42") == 0, "B 取 -> %s", S(line.p));

    close(b);
    buf_free(&in);
    buf_free(&line);
}

/* ════════════════════════════════════════════════════════════
 *  测试 8：多连接交错（验证每个连接的状态互不干扰）
 * ════════════════════════════════════════════════════════════ */
static void test_multi_conn(const char *ip, unsigned short port)
{
    printf("\n[8] %d 条连接交错读写\n", MULTI_CONNS);

    int   fds[MULTI_CONNS];
    buf_t in[MULTI_CONNS];
    buf_t line[MULTI_CONNS];
    int   opened = 0;

    memset(in,   0, sizeof(in));
    memset(line, 0, sizeof(line));

    for (int i = 0; i < MULTI_CONNS; i++)
    {
        fds[i] = kv_connect(ip, port);
        if (fds[i] < 0)
            break;
        opened++;
    }

    if (opened != MULTI_CONNS)
    {
        report(0, "只连上 %d/%d 条", opened, MULTI_CONNS);
        goto done;
    }

    /* 先把所有连接都发出去，不读 */
    for (int i = 0; i < MULTI_CONNS; i++)
    {
        char cmd[64];
        int  n = snprintf(cmd, sizeof(cmd), "SET mc%d val%d\n", i, i);
        if (send_all(fds[i], cmd, (size_t)n) != 0)
        {
            report(0, "连接 %d 发送失败", i);
            goto done;
        }
    }

    /* 再依次收 */
    int ok = 0;
    for (int i = 0; i < MULTI_CONNS; i++)
    {
        if (read_line(fds[i], &in[i], &line[i]) == 1
            && line[i].p && strcmp(line[i].p, "OK") == 0)
            ok++;
    }
    report(ok == MULTI_CONNS, "%d/%d 条连接都返回 OK", ok, MULTI_CONNS);

    /* 每条连接读别人的 key，确认数据没串 */
    int cross_ok = 0;
    for (int i = 0; i < MULTI_CONNS; i++)
    {
        int  j    = (i + 1) % MULTI_CONNS;      /* 故意读另一个连接的 key */
        char cmd[64], want[32];

        snprintf(cmd,  sizeof(cmd),  "GET mc%d\n", j);
        snprintf(want, sizeof(want), "val%d", j);

        if (kv_send_cmd(fds[i], &in[i], &line[i], cmd) == 1
            && line[i].p && strcmp(line[i].p, want) == 0)
            cross_ok++;
    }
    report(cross_ok == MULTI_CONNS, "%d/%d 条连接读到了正确的别人的数据", cross_ok, MULTI_CONNS);

done:
    for (int i = 0; i < opened; i++)
    {
        close(fds[i]);
        buf_free(&in[i]);
        buf_free(&line[i]);
    }
}

/* ════════════════════════════════════════════════════════════
 *  测试 9：长连接上连续发很多轮
 * ════════════════════════════════════════════════════════════ */
static void test_long_conn(const char *ip, unsigned short port)
{
    printf("\n[9] 同一条连接连发 %d 组 SET+GET\n", LONG_ROUNDS);

    int fd = kv_connect(ip, port);
    if (fd < 0) { report(0, "连接 %s:%u", ip, port); return; }

    buf_t in = {0}, line = {0};

    int good = 0;
    for (int i = 0; i < LONG_ROUNDS; i++)
    {
        char want[32];

        if (kv_send_fmt(fd, &in, &line, "SET ln %d\n", i) != 1 || line.p == NULL
            || strcmp(line.p, "OK") != 0)
        {
            printf("       第 %d 轮 SET 失败: %s\n", i, S(line.p));
            break;
        }

        snprintf(want, sizeof(want), "%d", i);
        if (kv_send_cmd(fd, &in, &line, "GET ln\n") != 1 || line.p == NULL
            || strcmp(line.p, want) != 0)
        {
            printf("       第 %d 轮 GET 失败: 期望 %s，实际 %s\n", i, want, S(line.p));
            break;
        }
        good++;
    }
    report(good == LONG_ROUNDS, "%d/%d 组读写正确", good, LONG_ROUNDS);

    buf_free(&in);
    buf_free(&line);
    close(fd);
}

/* ════════════════════════════════════════════════════════════
 *  main
 * ════════════════════════════════════════════════════════════ */
int main(int argc, char **argv)
{
    const char    *ip   = (argc > 1) ? argv[1] : "127.0.0.1";
    unsigned short port = (argc > 2) ? (unsigned short)atoi(argv[2]) : 8888;

    printf("=====================================================\n");
    printf(" flash_kv 测试客户端\n");
    printf(" 目标: %s:%u     超时: %d 秒\n", ip, port, IO_TIMEOUT_SEC);
    printf("=====================================================\n");

    test_basic      (ip, port);
    test_invalid    (ip, port);
    test_spaces     (ip, port);
    test_batch      (ip, port);
    test_split      (ip, port);
    test_big_value  (ip, port);
    test_cross_conn (ip, port);
    test_multi_conn (ip, port);
    test_long_conn  (ip, port);

    printf("\n=====================================================\n");
    printf(" 通过 %d 项，失败 %d 项\n", g_pass, g_fail);
    printf(" 结论: %s\n", (g_fail == 0) ? "全部通过 ✔" : "存在失败项 ✘");
    printf("=====================================================\n");

    return (g_fail == 0) ? 0 : 1;
}