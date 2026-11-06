/*
大块（size》4096）不会被回收，需要手动free，如果直接destory会造成内存泄漏
*/
#ifndef KVMEMCTL_H
#define KVMEMCTL_H
#include <stdint.h>
#include <stddef.h>
#define PAGE_SIZE 4096
#define KVP_LEVEL      9
#define KVP_HEAD_SIZE         8           /* 块头大小 */
#define KVP_FREE       0x5A5A  //23130 
#define KVP_USED        0xA5A5   //42405  ALIVE DEAD是按位取反的
#define KVP_CLS_LARGE   0xFFFF   //用来标记大块，便于free

#define KVP_MIN_CHUNK   4096        /* 一次至少要 4KB */
#define KVP_MIN_BLOCKS  8           /* 一次至少切 8 块（大档位需要） */

struct block_header //8byte
{
uint16_t level;
uint16_t magic_num;
uint32_t real_size;
};
_Static_assert(sizeof(struct block_header) == KVP_HEAD_SIZE, "块头必须 8 字节");


struct kvp_chunk
{
struct kvp_chunk* next;
void* mem;
};

struct kvp_pool
{
void* freelist;
size_t blocksize;
struct kvp_chunk* chunks;
};

struct kv_mempool_ctl
{
struct kvp_pool pool[KVP_LEVEL];
struct kvp_bigblock* bigblock;

};

void *kv_malloc(size_t size);
void  kv_free(void *ptr);
void kv_mempool_init(void);
int kv_mempool_caculatelevel(int size);
void kv_mempool_pushfront(int level, void *ptr);
void kv_mempool_popfront(int level);
int kv_mempool_refill(int level);
void kv_mempool_sethead(void *ptr, int level, size_t req);
void kv_mempool_destroy(void);
//全局变量
extern struct kv_mempool_ctl KV_mempool_ctl;

#endif
