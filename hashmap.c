#include "hashmap.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include "kvmemctl.h"
/* ================== 内部常量与类型(不进头文件) ================== */

#define HM_EMPTY   0   /* 空桶 */
#define HM_USED    1   /* 已占用 */
#define HM_DELETED 2   /* 墓碑:删除后留位,避免破坏探测链 */

#define HM_DEFAULT_CAP 16u   /* 默认初始容量 */

/* 每个桶:key + value + 状态标记 */
typedef struct hm_bucket {
    KEY_TYPE key;      /* char* 字符串键 */
    char    *value;    /* char* 字符串值 */
    unsigned char state;
} hm_bucket;

/* 表本体:对外只以不完整类型出现 */
struct hashmap {
    hm_bucket *buckets;
    size_t     cap;        /* 桶数量,恒为 2 的幂 */
    size_t     size;       /* 有效键值对个数(不含墓碑) */
    size_t     used;       /* 已用桶个数(含墓碑),used/cap 决定何时扩容 */
    int        last_error;
};

hashmap *hash=NULL;
/* ================== 内部工具 ================== */

/* 复制一个字符串到哈希表自己的内存里，失败返回 NULL。 */
static char *hm_strdup(const char *s)
{
    size_t n;
    char *copy;

    if (s == NULL) return NULL;

    n = strlen(s) + 1;
    copy = (char *)kv_malloc(n);
    if (copy == NULL) return NULL;

    memcpy(copy, s, n);
    return copy;
}

/* cap 向上取 2 的幂;0 → 1 */
static size_t hm_next_pow2(size_t v)
{
    if (v == 0) return 1;
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
#if SIZE_MAX > 0xFFFFFFFFu
    v |= v >> 32;
#endif
    return v + 1;
}

/* 字符串键哈希:FNV-1a */
static size_t hm_hash(KEY_TYPE key, size_t cap)
{
    const unsigned char *p = (const unsigned char *)key;
    unsigned long long h = 1469598103934665603ULL;

    if (key == NULL) return 0;
    while (*p != '\0') {
        h ^= (unsigned long long)*p++;
        h *= 1099511628211ULL;
    }
    return (size_t)h & (cap - 1);
}

/* 查找 key 所在桶下标;未找到返回 (size_t)-1。
 * *out_slot 可为 NULL;非 NULL 时返回"应插入位置"(第一个 EMPTY/DELETED)。 */
static size_t hm_find(const hashmap *m, KEY_TYPE key, hm_bucket **out_slot)
{
    size_t mask;
    size_t i;
    size_t first_free = (size_t)-1;
    size_t steps = 0;

    if (m == NULL || key == NULL) return (size_t)-1;

    mask = m->cap - 1;
    i    = hm_hash(key, m->cap);

    while (steps <= mask) {               /* 最多走 cap 步,防死循环 */
        hm_bucket *b = &m->buckets[i];
        if (b->state == HM_EMPTY) {
            if (out_slot && first_free == (size_t)-1) first_free = i;
            break;                        /* 探测链中断,必然不存在 */
        }
        if (b->state == HM_DELETED) {
            if (out_slot && first_free == (size_t)-1) first_free = i;
        } else if (b->key != NULL && strcmp(b->key, key) == 0) {
            if (out_slot) *out_slot = b;
            return i;
        }
        i = (i + 1) & mask;               /* 线性探测(回绕) */
        steps++;
    }
    if (out_slot)
        *out_slot = (first_free == (size_t)-1) ? NULL : &m->buckets[first_free];
    return (size_t)-1;
}

/* 扩容(或收缩)到 new_cap(2 的幂)并整体 rehash:墓碑在此被回收 */
static int hm_resize(hashmap *m, size_t new_cap)
{
    hm_bucket *nb = (hm_bucket *)calloc(new_cap, sizeof(hm_bucket));
    if (!nb) { m->last_error = HASHMAP_ERR_OOM; return -1; }

    hm_bucket *old = m->buckets;
    size_t     old_cap = m->cap;
    size_t     mask = new_cap - 1;
    size_t     i;

    for (i = 0; i < old_cap; i++) {
        if (old[i].state != HM_USED) continue;
        size_t j = hm_hash(old[i].key, new_cap);
        while (nb[j].state != HM_EMPTY)   /* 新表无墓碑,找到第一个空桶 */
            j = (j + 1) & mask;
        nb[j] = old[i];
    }

    free(old);
    m->buckets = nb;
    m->cap     = new_cap;
    m->used    = m->size;                 /* rehash 后墓碑全部消失 */
    return 0;
}

/* 负载因子超 0.7 扩容;失败时不致命(调用方可继续用旧表) */
static int hm_maybe_grow(hashmap *m)
{
    if (m->used * 10 < m->cap * 7) return 0;
    size_t want = m->cap * 2;
    if (want < m->cap) { m->last_error = HASHMAP_ERR_OOM; return -1; } /* 溢出 */
    return hm_resize(m, want);
}

/* ================== 对外接口 ================== */

hashmap *hashmap_create(size_t initial_cap)
{
    hashmap *m = (hashmap *)calloc(1, sizeof(hashmap));
    if (!m) return NULL;

    size_t cap = hm_next_pow2(initial_cap ? initial_cap : HM_DEFAULT_CAP);
    m->buckets = (hm_bucket *)calloc(cap, sizeof(hm_bucket));
    if (!m->buckets) { free(m); return NULL; }

    m->cap = cap;
    m->size = m->used = 0;
    m->last_error = HASHMAP_OK;
    return m;
}

void hashmap_destroy(hashmap *m)
{
    size_t i;

    if (!m) return;

    for (i = 0; i < m->cap; i++) {
        if (m->buckets[i].state == HM_USED) {
            kv_free(m->buckets[i].key);
            kv_free(m->buckets[i].value);
        }
    }

    free(m->buckets);
    free(m);
}

void hashmap_clear(hashmap *m)
{
    size_t i;

    if (!m) return;

    for (i = 0; i < m->cap; i++) {
        if (m->buckets[i].state == HM_USED) {
            kv_free(m->buckets[i].key);
            kv_free(m->buckets[i].value);
        }
        m->buckets[i].state = HM_EMPTY;
        m->buckets[i].key = NULL;
        m->buckets[i].value = NULL;
    }
    m->size = 0;
    m->used = 0;
    m->last_error = HASHMAP_OK;
}

int hashmap_put(hashmap *m, KEY_TYPE key, char *value)
{
    hm_bucket *slot = NULL;
    char *new_key;
    char *new_value;

    if (!m || key == NULL || value == NULL) return -1;

    hm_find(m, key, &slot);

    if (slot && slot->state == HM_USED && strcmp(slot->key, key) == 0) {
        char *value_copy = hm_strdup(value);
        if (value_copy == NULL) {
            m->last_error = HASHMAP_ERR_OOM;
            return -1;
        }
        kv_free(slot->value);
        slot->value = value_copy;
        m->last_error = HASHMAP_OK;
        return 0;
    }

    if (!slot) {                          /* 表被删满占满,先扩容再重找 */
        /* FIX:原来不区分 OOM 和"表满",可能在 OOM 时误记成 FULL */
        if (hm_maybe_grow(m) != 0) return -1;
        hm_find(m, key, &slot);
        if (!slot) { m->last_error = HASHMAP_ERR_FULL; return -1; }  /* 理论上不可达 */
    } else if (m->used * 10 >= m->cap * 7) {
        /* FIX:必须先扩容再写 slot。hm_maybe_grow 会 rehash 整张表,
         * 原来的 slot 指针在扩容后已经失效(use-after-free)。 */
        if (hm_maybe_grow(m) != 0) return -1;
        slot = NULL;
        hm_find(m, key, &slot);
        if (!slot) { m->last_error = HASHMAP_ERR_FULL; return -1; }
    }

    new_key = hm_strdup(key);
    new_value = hm_strdup(value);
    if (new_key == NULL || new_value == NULL) {
        kv_free(new_key);
        kv_free(new_value);
        m->last_error = HASHMAP_ERR_OOM;
        return -1;
    }

    int reused_tombstone = (slot->state == HM_DELETED);

    slot->key    = new_key;
    slot->value  = new_value;
    slot->state  = HM_USED;
    m->size++;
    if (!reused_tombstone) m->used++;     /* 复用墓碑不增加 used */

    m->last_error = HASHMAP_OK;
    return 1;
}

int hashmap_remove(hashmap *m, KEY_TYPE key)
{
    hm_bucket *slot = NULL;

    if (!m || key == NULL) return 0;
    hm_find(m, key, &slot);
    if (!slot || slot->state != HM_USED) {
        m->last_error = HASHMAP_ERR_NOTFOUND;
        return 0;
    }
    slot->state = HM_DELETED;             /* 墓碑保留,探测链不断裂 */
    kv_free(slot->key);                   /* 自动释放内部 key/value */
    kv_free(slot->value);
    slot->key = NULL;
    slot->value = NULL;
    m->size--;
    m->last_error = HASHMAP_OK;
    return 1;
}

char *hashmap_get(const hashmap *m, KEY_TYPE key)
{
    hm_bucket *slot = NULL;

    if (!m || key == NULL) return NULL;
    hm_find(m, key, &slot);
    return (slot && slot->state == HM_USED) ? slot->value : NULL;
}

int hashmap_contains(const hashmap *m, KEY_TYPE key)
{
    hm_bucket *slot = NULL;

    if (!m || key == NULL) return 0;
    hm_find(m, key, &slot);
    return (slot && slot->state == HM_USED) ? 1 : 0;
}

size_t hashmap_size(const hashmap *m) { return m ? m->size : 0; }

int hashmap_empty(const hashmap *m) { return (!m || m->size == 0) ? 1 : 0; }

int hashmap_foreach(const hashmap *m, hashmap_iter_fn fn, void *userdata)
{
    if (!m || !fn) return 0;
    size_t i;
    for (i = 0; i < m->cap; i++) {
        if (m->buckets[i].state != HM_USED) continue;
        int r = fn(m->buckets[i].key, m->buckets[i].value, userdata);
        if (r) return r;                  /* 回调要求终止 */
    }
    return 0;
}

size_t hashmap_keys(const hashmap *m, KEY_TYPE *out_keys)
{
    if (!m || !out_keys) return 0;
    size_t n = 0, i;
    for (i = 0; i < m->cap; i++)
        if (m->buckets[i].state == HM_USED) out_keys[n++] = m->buckets[i].key;
    return n;
}

int hashmap_reserve(hashmap *m, size_t n)
{
    if (!m) return -1;
    /* 需要的桶数:按负载因子 0.7 反推,再向上取 2 的幂 */
    size_t need = (size_t)((double)n / 0.7) + 1;
    size_t want = hm_next_pow2(need);
    if (want <= m->cap) { m->last_error = HASHMAP_OK; return 0; }
    return hm_resize(m, want);
}

int hashmap_last_error(const hashmap *m)
{
    return m ? m->last_error : HASHMAP_ERR_NOTFOUND;
}


///////////////////////
int hash_split_command(char *input, char *output[]) // 返回值为解析出来的单词数目
{
    int count = 0;
    char *p = input;

    while (*p)
    {
        /* 1. 跳过所有空白 */
        while (*p && isspace((unsigned char)*p))
            p++;

        /* 2. 到达字符串末尾，结束 */
        if (*p == '\0')
            break;

        /* 3. 记录这个单词的开始 */
        output[count++] = p;

        /* 4. 向后扫描直到遇到空白 */
        while (*p != '\0' && !isspace((unsigned char)*p))
            p++;

        /* 5. 把分隔符替换成 '\0'，以便单词独立成串 */
        if (*p)
        {
            *p = '\0';
            p++; /* 跳到下一个位置继续找 */
        }
    }

    return count;
}
int kvs_hash_protocol(char *msg, char *response)
{
    char *tokens[16] = {0};
    int count = hash_split_command(msg, tokens);
    if (count == 0 || tokens[0] == NULL) // 空输入保护
    {
        sprintf(response, "ERROR");
        return strlen(response);
    }
    char *command = tokens[0];
        if (strcmp(command, "SET") == 0)
    {

        if (count < 3 || tokens[1] == NULL || tokens[2] == NULL)
        {
            sprintf(response, "INVALID");
            return strlen(response);
        }
        int rc=hashmap_put(hash,tokens[1],tokens[2]);
        if(rc<0)
        {
            sprintf(response,"WRONG");
            return strlen(response);
        }
        sprintf(response,"OK");
        return strlen(response);
    }
    if (strcmp(command, "GET") == 0)
    {
        if (count < 2 || tokens[1] == NULL)
        {
            sprintf(response, "INVALID");
            return strlen(response);
        }
        char *key = tokens[1];
        char* get = hashmap_get(hash, key);
        if (get == NULL)
        {
            sprintf(response, "INEXIST");
            return strlen(response);
        }
        
        sprintf(response, "%s", get);
        return strlen(response);
    }
    if (strcmp(command, "DEL") == 0)
    {
        if (count < 2 || tokens[1] == NULL)
        {
            sprintf(response, "INVALID");
            return strlen(response);
        }
        char *key = tokens[1];
        int res=hashmap_remove(hash,key);
        if(res==0)
        {
            sprintf(response, "ERROR");
            return strlen(response);            
        }
        sprintf(response, "OK");
        return strlen(response);
    }
    if (strcmp(command, "MOD") == 0)
    {
    if (count < 3 || tokens[1] == NULL || tokens[2] == NULL)
    {
        strcpy(response, "INVALID");
        return (int)strlen(response);
    }

    if (!hashmap_contains(hash, tokens[1]))
    {
        strcpy(response, "INEXIST");
        return (int)strlen(response);
    }

    if (hashmap_put(hash, tokens[1], tokens[2]) < 0)
    {
        strcpy(response, "ERROR");
        return (int)strlen(response);
    }

    strcpy(response, "OK");
    return (int)strlen(response);
    }
    if (strcmp(command, "EXIST") == 0)
    {
        if (count < 2 || tokens[1] == NULL)
        {
            sprintf(response, "INVALID");
            return strlen(response);
        }
        int flag=hashmap_contains(hash,tokens[1]);
        if(flag>0)
        {
        sprintf(response, "YES");
        return strlen(response);
        }
        else
        {
            sprintf(response,"NO");
            return strlen(response);
        }
    }
    sprintf(response, "INVALID");
    return strlen(response);
}