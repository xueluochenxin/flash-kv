#ifndef KVMEMCTL_H
#define KVMEMCTL_H

#include <stddef.h>

/*
 * KV 内存分配统一入口。
 * 目前只是 malloc/free 的薄封装，后续可以在这里换成
 * 内存池、slab、统计、对齐分配等实现。
 */
void *kv_malloc(size_t size);
void  kv_free(void *ptr);

#endif /* KVMEMCTL_H */
