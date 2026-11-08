#include "kvbuf.h"
#include <stdlib.h>
#include <string.h>
/* ── 应答缓冲的上界：见过的最大命令长度 ── */
static size_t g_max_cmd_len = 256;
void kvbuf_init(kvbuf* b)
{
b->cap=0;
b->len=0;
b->p=NULL;
}
void kvbuf_destroy(kvbuf* b)
{
    free(b->p);       
    b->p   = NULL;
    b->len = 0;
    b->cap = 0;
}
void kvbuf_reset(kvbuf* b) //清空
{
b->len=0;
}
int  kvbuf_reserve(kvbuf *b, size_t need)//return 0成功，-1失败
{
    if (need <= b->cap)         
        return 0;

    size_t ncap = b->cap ? b->cap * 2 : KV_BUF_MIN_CAP;
    if (ncap < need)            /* 要得比"翻倍"还多，就直接按需给 */
        ncap = need;

    char *np = realloc(b->p, ncap);
    if (np == NULL)             /* 失败：b->p 原样保留，数据还在 */
        return -1;

    b->p   = np;
    b->cap = ncap;
    return 0;
}
int  kvbuf_append(kvbuf *b, const char *data, size_t n)
{
    if (n == 0)
        return 0;

    if (b->len + n < b->len)    /* len+n 溢出，说明要的字节数超过 size_t */
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
                  int (*handler)(char *msg, char *response))
{
    size_t start = 0;

    while (1) {
        char  *base  = rbuf->p + start;
        size_t avail = rbuf->len - start;

        /* 1. 找一行 */
        char *nl = memchr(base, '\n', avail);
        if (nl == NULL)
            break;                      /* 没收到完整的，留着等下次 */

        size_t len = (size_t)(nl - base);
        *nl = '\0';                     /* 原地截断 */

        if (len == 0) {                 /* 空行 */
            start += 1;
            continue;
        }

        /* 2. 更新应答上界 */
        kv_note_cmd_len(len);

        /* 3. 给应答留够地方，注意是追加到 sbuf 末尾 */
        if (kvbuf_reserve(sbuf, sbuf->len + g_max_cmd_len) != 0)
            return -1;

        /* 4. 交给协议层 */
        int num = handler(base, sbuf->p + sbuf->len);
        if (num <= 0)
            return -1;

        sbuf->len += (size_t)num;       /* 应答累积，不是发出去 */
        start += len + 1;
    }

    /* 5. 把处理过的字节挪走，剩下的留到下次 */
    if (start > 0) {
        memmove(rbuf->p, rbuf->p + start, rbuf->len - start);
        rbuf->len -= start;
    }
    return 0;
}