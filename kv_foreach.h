#ifndef KV_FOREACH_H
#define KV_FOREACH_H

/*
 * 统一遍历接口。
 *
 * fn 的返回值：
 *   0   → 继续遍历
 *   非0 → 提前终止，该值作为 foreach 的返回值透传
 *
 * foreach 返回值：
 *   0   → 全部遍历完成
 *   非0 → 回调要求提前终止
 *  -1   → 参数错误
 *
 * key/value 是引擎内部指针：只读，不要 free；
 * 遍历期间禁止 insert/erase/destroy。
 */
typedef int (*kv_visit_fn)(char *key, char *value, void *arg);

int kvs_skiplist_foreach(kv_visit_fn fn, void *arg);
int kvs_hash_foreach    (kv_visit_fn fn, void *arg);
int kvs_rbtree_foreach  (kv_visit_fn fn, void *arg);
int kvs_array_foreach   (kv_visit_fn fn, void *arg);

/* 按 STORE_ENGINE 分发（实现在 flash_kvstore.c） */
int kv_store_foreach(kv_visit_fn fn, void *arg);
void kv_store_clear(void);//清空整个数据集
void kv_store_engine_create(void);
#endif /* KV_FOREACH_H */