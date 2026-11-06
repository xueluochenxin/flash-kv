#include "kvmemctl.h"

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <stdio.h>
/// //
// 全局变量

struct kv_mempool_ctl KV_mempool_ctl;
static const size_t kClassSize[KVP_LEVEL] = {
    16, 32, 64, 128, 256, 512, 1024, 2048, 4096};
//
void kv_mempool_init(void)
{
    memset(&KV_mempool_ctl, 0, sizeof(struct kv_mempool_ctl));
    KV_mempool_ctl.bigblock = NULL;
    for (int i = 0; i < KVP_LEVEL; ++i)
    {
        KV_mempool_ctl.pool[i].blocksize = kClassSize[i];
        KV_mempool_ctl.pool[i].chunks = NULL;
        KV_mempool_ctl.pool[i].freelist = NULL;
    }
}

int kv_mempool_caculatelevel(int size) // 返回值小于0代表出错，返回KVP_CLS_LARGE表示需要单独malloc
{
    if (size > PAGE_SIZE - KVP_HEAD_SIZE)
    {
        return KVP_CLS_LARGE;
    }
    if (size < 0)
    {
        return -1;
    }
    int i = 0;
    while (i<KVP_LEVEL)
    {
        if ((size_t)size <= kClassSize[i] - 8)
        {
            break;
        }
        else
        {
            i++;
        }
    }
    return (i < KVP_LEVEL) ? i : KVP_CLS_LARGE;
}
void kv_mempool_pushfront(int level, void *ptr) // 不操控大块
{
    *(void **)((char *)ptr + KVP_HEAD_SIZE) = KV_mempool_ctl.pool[level].freelist;
    KV_mempool_ctl.pool[level].freelist = ptr;
}
void kv_mempool_popfront(int level) // 把头给删除
{
    KV_mempool_ctl.pool[level].freelist = *(void **)((char *)KV_mempool_ctl.pool[level].freelist + KVP_HEAD_SIZE);
}
int kv_mempool_refill(int level)
{
    int block_size = KV_mempool_ctl.pool[level].blocksize;
    int times = PAGE_SIZE / block_size;
    if (times < KVP_MIN_BLOCKS) { times = KVP_MIN_BLOCKS; }
    struct kvp_chunk *chunk = malloc(sizeof(struct kvp_chunk));
    if (chunk == NULL)
    {
        return -1;
    }
    chunk->next = KV_mempool_ctl.pool[level].chunks;
    void *ptr = malloc((size_t)times * block_size);
    if (ptr == NULL)
    {
        free(chunk);
        return -1;
    }
    chunk->mem = ptr;
    KV_mempool_ctl.pool[level].chunks = chunk;
    for (int i = 0; i < times; ++i)
    {
        kv_mempool_pushfront(level, (char *)ptr + block_size * i);
    }
    return 0;
}
void kv_mempool_sethead(void *ptr, int level, size_t req)
{
    struct block_header head;
    head.level = level;
    head.magic_num = KVP_USED;
    head.real_size = (uint32_t)req;
    memcpy(ptr, &head, sizeof(head));
}
void *kv_malloc(size_t size)
{
    int result = kv_mempool_caculatelevel(size);
    if (result < 0)
    {
        return NULL;
    }
    if (result == KVP_CLS_LARGE)
    {
       void* ptr=malloc(size+KVP_HEAD_SIZE);
       if(ptr==NULL){return NULL;}
       kv_mempool_sethead(ptr,result,size);
       return (void*)((char*)ptr+KVP_HEAD_SIZE);
    }
    else if (result != KVP_CLS_LARGE && KV_mempool_ctl.pool[result].freelist == NULL)
    {
        if (kv_mempool_refill(result) != 0)
            return NULL;
    }

    void *ptr = KV_mempool_ctl.pool[result].freelist;
    KV_mempool_ctl.pool[result].freelist = *(void **)((char *)ptr + KVP_HEAD_SIZE);
    kv_mempool_sethead(ptr, result, size);
    //kv_mempool_popfront(result);
    ptr = (char *)ptr + KVP_HEAD_SIZE;
    return ptr;
}

void kv_free(void *ptr)
{
    if (ptr == NULL)
    {
        return;
    }
    struct block_header *head = (struct block_header *)((char *)ptr - KVP_HEAD_SIZE);
    int level = head->level;
    if (head->magic_num != KVP_USED)
    {
        fprintf(stderr, "kv_free: 重复释放或野指针 %p\n", ptr);
        abort();
    }
    if (level != KVP_CLS_LARGE && (level < 0 || level >= KVP_LEVEL))
    {
        fprintf(stderr, "kv_free: 块头 level 越界 %d %p\n", level, ptr);
        abort();
    }

    if (level == KVP_CLS_LARGE)
    {
        //struct kvp_bigblock **pp = &KV_mempool_ctl.bigblock;
        void *base = (char *)ptr - KVP_HEAD_SIZE;
        free(base);
        
        //fprintf(stderr, "kv_free: 大块指针不在链表里 %p\n", ptr);
    }
    else
    {
        head->magic_num = KVP_FREE;
        kv_mempool_pushfront(level, head);
    }
}
void kv_mempool_destroy(void)
{
    // struct kvp_bigblock *bigblock = KV_mempool_ctl.bigblock;
    // while (bigblock != NULL)
    // {
    //     struct kvp_bigblock *temp = bigblock;
    //     bigblock = bigblock->next;
    //     free(temp->mem);
    //     free(temp);
    // }
    KV_mempool_ctl.bigblock = NULL;
    // struct kvp_chunk *chunkptr = KV_mempool_ctl.pool->chunks;
    // while (chunkptr != NULL)
    // {
    //     struct kvp_chunk *tempptr = chunkptr;
    //     chunkptr = chunkptr->next;
    //     free(tempptr->mem);
    // }
    for (int i = 0; i < KVP_LEVEL; i++) {
        struct kvp_chunk *ck = KV_mempool_ctl.pool[i].chunks;
        while (ck) {
            struct kvp_chunk *nx = ck->next;
            free(ck->mem);      /* 整个 chunk 还掉，里面活着的块不用管 */
            free(ck);
            ck = nx;
        }
        KV_mempool_ctl.pool[i].chunks   = NULL;
        KV_mempool_ctl.pool[i].freelist = NULL;
}
}
