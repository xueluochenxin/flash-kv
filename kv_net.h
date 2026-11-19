#ifndef KV_NET_H
#define KV_NET_H

#include <stddef.h>

/* ── 把 len 字节追到 fd 这条连接的待发队列，并尽力启动发送 ──
   返回 0：已入队（网络层保证会发出去，顺序也对）
   返回 -1：失败（连接已废） */
int kv_net_push(int fd, const char *buf, size_t len);

/* ── 关闭连接（网络层自己清理待发队列） ── */
void kv_net_close(int fd);

#endif /* KV_NET_H */