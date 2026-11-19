#include "kvbuf.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
/* ── 应答缓冲的上界：见过的最大命令长度 ── */
static size_t g_max_cmd_len = 256;
static kvbuf g_linebuf;             // 给protocol解析用的
static kvbuf g_tmpbuf;              // 接受引擎的返回
static kv_aof_fn g_aof_hook = NULL; // kvbuf.c不负责写，文件路径等都保存在flash_kvstore里面。这里只负责通知，所以叫钩子
static kv_sync_fn g_sync_hook = NULL;

void kv_set_sync_hook(kv_sync_fn fn)
{
    g_sync_hook = fn;
}
void kv_set_aof_hook(kv_aof_fn fn)
{
    g_aof_hook = fn;
}
static int kv_is_write_cmd(const char *base, size_t len)
{
    size_t i = 0;

    while (i < len && isspace((unsigned char)base[i]))
        i++; /* 跳前导空白 */

    const char *p = base + i;
    size_t left = len - i;

    if (left >= 3 && memcmp(p, "SET", 3) == 0 &&
        (left == 3 || isspace((unsigned char)p[3]))) // 至少三个字节，set后面是空白
        return 1;

    if (left >= 3 && memcmp(p, "MOD", 3) == 0 &&
        (left == 3 || isspace((unsigned char)p[3])))
        return 1;

    if (left >= 3 && memcmp(p, "DEL", 3) == 0 &&
        (left == 3 || isspace((unsigned char)p[3])))
        return 1;

    return 0;
}
static int kvbuf_terminate(kvbuf *b)
{
    if (kvbuf_reserve(b, b->len + 1) != 0)
        return -1;

    b->p[b->len] = '\0';
    return 0;
}
static int parse_setb_head(const char *line, size_t len,
                           const char **key, size_t *keylen, size_t *vlen) // 解析头部，成功返回0
{
    size_t i = 5; /* 跳过 "SETB " */

    /* ---- key：到下一个空白为止 ---- */
    size_t kstart = i;
    while (i < len && line[i] != ' ' && line[i] != '\t')
        i++;

    if (i == kstart) /* 没有 key */
        return -1;

    *key = line + kstart;
    *keylen = i - kstart;

    /* ---- 跳过空白 ---- */
    while (i < len && (line[i] == ' ' || line[i] == '\t'))
        i++;

    if (i >= len) /* 没有长度字段 */
        return -1;

    /* ---- 十进制长度 ----
       不要用 strtoul：这一行还没被 '\0' 截断，
       strtoul 会越过 len 一直往后读，读到正文里去 */
    size_t v = 0;
    size_t nstart = i;

    while (i < len)
    {
        if (line[i] < '0' || line[i] > '9')
            return -1;

        if (v > ((size_t)-1 - 9) / 10) /* 防溢出 */
            return -1;

        v = v * 10 + (size_t)(line[i] - '0');
        i++;
    }

    if (i == nstart)
        return -1;

    *vlen = v;
    return 0;
}
void kvbuf_init(kvbuf *b)
{
    b->cap = 0;
    b->len = 0;
    b->p = NULL;
}
void kvbuf_destroy(kvbuf *b)
{
    free(b->p);
    b->p = NULL;
    b->len = 0;
    b->cap = 0;
}
void kvbuf_reset(kvbuf *b) // 清空
{
    b->len = 0;
}
int kvbuf_reserve(kvbuf *b, size_t need) // return 0成功，-1失败
{
    if (need <= b->cap)
        return 0;

    size_t ncap = b->cap ? b->cap * 2 : KV_BUF_MIN_CAP;
    if (ncap < need) /* 要得比"翻倍"还多，就直接按需给 */
        ncap = need;

    char *np = realloc(b->p, ncap);
    if (np == NULL) /* 失败：b->p 原样保留，数据还在 */
        return -1;

    b->p = np;
    b->cap = ncap;
    return 0;
}
int kvbuf_append(kvbuf *b, const char *data, size_t n)
{
    if (n == 0)
        return 0;

    if (b->len + n < b->len) /* len+n 溢出，说明要的字节数超过 size_t */
        return -1;

    if (kvbuf_reserve(b, b->len + n) != 0)
        return -1;

    memcpy(b->p + b->len, data, n);
    b->len += n;
    return 0;
}

void kv_note_cmd_len(size_t len)
{
    if (len + 64 > g_max_cmd_len)
        g_max_cmd_len = len + 64;
}

int kvbuf_process(kvbuf *rbuf, kvbuf *sbuf,
                  int (*handler)(char *msg, char *response), int conn_fd)
{
    size_t start = 0;

    while (1)
    {
        char *base = rbuf->p + start;
        size_t avail = rbuf->len - start;

        /* 1. 找一行 */
        char *nl = memchr(base, '\n', avail);
        if (nl == NULL)
            break; /* 没收到完整的，留着等下次 */

        size_t len = (size_t)(nl - base);

        if (len > 5 && memcmp(base, "SETB ", 5) == 0)
        {
            const char *key;
            size_t keylen, vlen;

            /*  解析头部 */
            if (parse_setb_head(base, len, &key, &keylen, &vlen) != 0)
            {
                if (kvbuf_append(sbuf, "INVALID\n", 8) != 0)
                    return -1;
                start += len + 1;
                continue; // 头部处理完毕，继续循环去recv value
            }

            /*  正文还没收全 → break，start 绝对不能动 */
            size_t need = len + 1 + vlen; // need长度就是head+换行符+key的长度，也就是需要的总长度
            if (avail < need)
                break; // 继续去recv

            /*  正文含 '\0'：当前不支持，明确报错，别静默截断 */
            if (memchr(base + len + 1, '\0', vlen) != NULL)
            {
                if (kvbuf_append(sbuf, "INVALID\n", 8) != 0)
                    return -1;
                start += need; /* 正文也吃掉 */
                if (start < rbuf->len && rbuf->p[start] == '\n')
                    start += 1;
                continue;
            }
            if (g_aof_hook != NULL)
                g_aof_hook(base, need);
            /*  组装成一条普通命令："SET <key> <正文>"，引擎无感知 */
            g_linebuf.len = 0;
            if (kvbuf_append(&g_linebuf, "SET ", 4) != 0)
                return -1;
            if (kvbuf_append(&g_linebuf, key, keylen) != 0)
                return -1;
            if (kvbuf_append(&g_linebuf, " ", 1) != 0)
                return -1;
            if (kvbuf_append(&g_linebuf, base + len + 1, vlen) != 0)
                return -1;
            if (kvbuf_terminate(&g_linebuf) != 0)
                return -1;

            /* 必须更新应答上界，否则后面 GETB 取大 value 会写爆 sbuf */
            kv_note_cmd_len(g_linebuf.len);

            /*  交给引擎 */
            if (kvbuf_reserve(sbuf, sbuf->len + g_max_cmd_len) != 0)
                return -1;

            int num = handler(g_linebuf.p, sbuf->p + sbuf->len);
            if (num <= 0)
                return -1;

            sbuf->len += (size_t)num;

            /*  消费：头部 + '\n' + 正文 */
            start += need;
            if (start < rbuf->len && rbuf->p[start] == '\n')
                start += 1; /* 容忍块尾 '\n' */
            continue;
        }
        if (len > 5 && memcmp(base, "GETB ", 5) == 0)
        {
            const char *key = base + 5;
            size_t keylen = len - 5;

            /*  改写成 "GET <key>" */
            g_linebuf.len = 0;
            if (kvbuf_append(&g_linebuf, "GET ", 4) != 0)
                return -1;
            if (kvbuf_append(&g_linebuf, key, keylen) != 0)
                return -1;
            if (kvbuf_terminate(&g_linebuf) != 0)
                return -1;

            /* 引擎应答写临时缓冲 —— 不能直接写 sbuf，
                  因为要在它前面插一个此刻还不知道长度的 "$<vlen>\n" */
            if (kvbuf_reserve(&g_tmpbuf, g_max_cmd_len) != 0)
                return -1;

            int num = handler(g_linebuf.p, g_tmpbuf.p);
            if (num <= 0)
                return -1;

            /*  INEXIST 原样透传。
                  两个条件都要：只判长度会把"7 字节且以换行结尾的值"误判 */
            if (num == 8 && memcmp(g_tmpbuf.p, "INEXIST\n", 8) == 0)
            {
                if (kvbuf_append(sbuf, "INEXIST\n", 8) != 0)
                    return -1;
            }
            /*  引擎给的是 "value\n"，去掉末尾那个 '\n' */
            else
            {
                size_t vlen = (size_t)num - 1;
                char head[32];
                int hn = snprintf(head, sizeof(head), "$%zu\n", vlen);

                if (hn <= 0 || (size_t)hn >= sizeof(head))
                    return -1;
                if (kvbuf_append(sbuf, head, (size_t)hn) != 0)
                    return -1;
                if (kvbuf_append(sbuf, g_tmpbuf.p, vlen) != 0)
                    return -1;
                if (kvbuf_append(sbuf, "\n", 1) != 0)
                    return -1;
            }

            start += len + 1;
            continue;
        }

        if (g_sync_hook != NULL && len >= 4 && memcmp(base, "SYNC", 4) == 0 && (len == 4 || isspace((unsigned char)base[4])))
        {
            if (g_sync_hook(conn_fd,sbuf) != 0)
                return -1;
            start += len + 1;
            continue;
        }
        
        if (g_aof_hook != NULL && kv_is_write_cmd(base, len))
            g_aof_hook(base, len + 1); /* 含 '\n'，原样 */
        *nl = '\0';                    /* 原地截断 */
        size_t l = 0, r = len;
        while (l < r && isspace((unsigned char)base[l]))
            l++; /* 跳过前导空白 */
        while (r > l && isspace((unsigned char)base[r - 1]))
            r--; /* 去掉尾部空白（含 '\r'） */

        if (l == r) /* 空行 / 全是空白 */
        {
            start += len + 1;
            continue;
        }

        base += l;           /* ← 只改局部指针，不影响 start */
        size_t clen = r - l; /* 裁剪后的长度 */

        /* 2. 更新应答上界 */
        kv_note_cmd_len(len);

        /* 3. 给应答留够地方，注意是追加到 sbuf 末尾 */
        if (kvbuf_reserve(sbuf, sbuf->len + g_max_cmd_len) != 0)
            return -1;

        /* 4. 交给协议层 */
        int num = handler(base, sbuf->p + sbuf->len);
        if (num <= 0)
            return -1;

        sbuf->len += (size_t)num; /* 应答累积，不是发出去 */
        start += len + 1;
    }

    /* 5. 把处理过的字节挪走，剩下的留到下次 */
    if (start > 0)
    {
        memmove(rbuf->p, rbuf->p + start, rbuf->len - start);
        rbuf->len -= start;
    }
    return 0;
}