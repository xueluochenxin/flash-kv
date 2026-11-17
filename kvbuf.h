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
int kvbuf_process(kvbuf *rbuf, kvbuf *sbuf,
                  int (*handler)(char *msg, char *response),int conn_fd);
void kv_note_cmd_len(size_t len);

//
typedef void (*kv_aof_fn)(const char *raw, size_t len);
void kv_set_aof_hook(kv_aof_fn fn);      /* 传 NULL 取消 */
typedef int (*kv_sync_fn)(int conn_fd);
void kv_set_sync_hook(kv_sync_fn fn);   /* 传 NULL 取消 */

//protocol 的第二个参数 response 指向一块按"最长命令长度"预留的缓冲。
//全量数据比它大几个数量级，必然溢出。所以 SYNC 要开一条独立的路，直接 nty_send。


#endif