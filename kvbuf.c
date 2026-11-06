#include "kvbuf.h"
#include <stdlib.h>
#include <string.h>
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