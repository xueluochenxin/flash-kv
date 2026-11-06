#ifndef KVBUF_H
#define KVBUF_H

#include <stddef.h>
#define KV_BUF_MIN_CAP 256
typedef struct kvbuf {
    char   *p;
    size_t  len;
    size_t  cap;
} kvbuf;

void kvbuf_init(kvbuf *b);
void kvbuf_destroy(kvbuf *b);
void kvbuf_reset(kvbuf *b);
int  kvbuf_reserve(kvbuf *b, size_t need);
int  kvbuf_append(kvbuf *b, const char *data, size_t n);

#endif