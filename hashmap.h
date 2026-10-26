#ifndef HASHMAP_H
#define HASHMAP_H

/* ============================================================================
 * hashmap.h —— 开放寻址(线性探测)哈希表库
 *
 * 设计要点:
 *   - 键:   char*(NUL 结尾字符串)
 *   - 值:   char*(NUL 结尾字符串)
 *   - 所有权: hashmap_put 会复制 key/value；
 *             hashmap_remove / hashmap_clear / hashmap_destroy
 *             会自动释放哈希表内部的 key/value。
 *             调用方传给 put 的字符串在 put 返回后可以自行释放。
 *   - 读取: hashmap_get / hashmap_foreach / hashmap_keys 返回内部指针，
 *           只读，不要 free；在下一次 put/remove/clear/destroy 前有效。
 *   - 存储: 数组 + 线性探测,负载因子上限 0.7 自动扩容(容量翻倍)
 *   - 删除: 惰性删除(DELETED 墓碑标记),rehash 时统一回收空位
 *   - 线程不安全,单线程使用
 *
 * 用法示例:
 *   hashmap *m = hashmap_create(0);
 *   hashmap_put(m, "name", "flash");     // 表内部复制 key/value
 *   char *got = hashmap_get(m, "name");  // 返回内部指针，只读
 *   hashmap_remove(m, "name");           // 自动释放内部 key/value
 *   hashmap_destroy(m);                  // 自动释放所有剩余 key/value
 *
 * 对外提供的接口(全部以 hashmap_ 前缀命名,不会与其它库冲突):
 *   创建/销毁  : hashmap_create, hashmap_destroy, hashmap_clear
 *   增/删/改   : hashmap_put, hashmap_remove
 *   查/读      : hashmap_get, hashmap_contains, hashmap_size, hashmap_empty
 *   遍历       : hashmap_foreach, hashmap_keys
 *   扩展       : hashmap_reserve
 *   错误处理   : hashmap_last_error
 * ==========================================================================*/

#include <stddef.h>

/* ---------- 键类型固定为 char*;可在 include 前覆盖,但必须兼容 strcmp ---------- */
#ifndef KEY_TYPE
#define KEY_TYPE char *
#endif

/* 表状态:供 hashmap_last_error() 返回 */
#define HASHMAP_OK            0   /* 上次操作成功 */
#define HASHMAP_ERR_OOM       1   /* 内存分配失败,操作未生效 */
#define HASHMAP_ERR_FULL      2   /* 表已满且无法扩容(仅 hashmap_reserve 触发) */
#define HASHMAP_ERR_NOTFOUND  3   /* 键不存在(remove/put 读取用) */

/* 遍历回调:对每个 (key, value) 调用一次。
 * key/value 都是表内部的 char*;返回 0 继续,返回非 0 提前终止。
 * 不要 free 这两个指针。 */
typedef int (*hashmap_iter_fn)(KEY_TYPE key, char *value, void *userdata);

/* 对外暴露的哈希表结构体(字段视为内部实现,请勿直接修改) */
typedef struct hashmap hashmap;

/* ======================= 对外接口 ======================= */

/* --- 创建 / 销毁 --- */

/* 创建哈希表。initial_cap 传 0 使用默认容量(16);超过 0 则向上取 2 的幂。
 * 成功返回表指针,内存不足返回 NULL。 */
hashmap *hashmap_create(size_t initial_cap);

/* 销毁整张表。
 * 对每个仍然存在的 key/value 调用 kv_free,然后释放桶数组和表本身。
 * 销毁后指针不可再使用。 */
void hashmap_destroy(hashmap *m);

/* 清空表内所有键值对(保留已分配的容量,不缩小)。
 * 对每个仍然存在的 key/value 调用 kv_free。
 * 清空后表仍然可用,可以继续 put。 */
void hashmap_clear(hashmap *m);

/* --- 增 / 删 / 改 --- */

/* 插入或更新 key。
 * 本函数会复制 key 和 value,调用方不需要保留传入的字符串。
 * value 不能为 NULL。
 * 返回 1:插入成功
 * 返回 0:key 已存在,更新了 value(旧 value 已自动释放)
 * 返回 -1:参数非法或内存分配失败(见 hashmap_last_error) */
int hashmap_put(hashmap *m, KEY_TYPE key, char *value);

/* 删除 key。
 * 成功返回 1,并自动释放表内部的 key/value。
 * key 不存在返回 0。 */
int hashmap_remove(hashmap *m, KEY_TYPE key);

/* --- 查 / 读 --- */

/* 取 key 对应的值;不存在返回 NULL。
 * 返回的是表内部指针,只读,不要 free；
 * 在下一次 put/remove/clear/destroy 前有效。 */
char *hashmap_get(const hashmap *m, KEY_TYPE key);

/* 判断 key 是否存在,返回 1 / 0。 */
int hashmap_contains(const hashmap *m, KEY_TYPE key);

/* 返回键值对数量。 */
size_t hashmap_size(const hashmap *m);

/* 表为空返回 1,否则 0。 */
int hashmap_empty(const hashmap *m);

/* --- 遍历 --- */

/* 依次对每个 (key, value) 调用 fn;fn 返回非 0 时提前结束。
 * key/value 是表内部指针,不要 free。
 * 遍历期间禁止 put/remove/clear/destroy。
 * fn 返回累计的非零值,全遍历完返回 0。 */
int hashmap_foreach(const hashmap *m, hashmap_iter_fn fn, void *userdata);

/* 把所有 key 指针拷贝到 out_keys 数组(容量至少 hashmap_size(m))。
 * 返回实际写入个数。
 * out_keys 由调用方分配；数组里是表内部的 key 指针,不要 free,
 * 并且在 put/remove/clear/destroy 前有效。 */
size_t hashmap_keys(const hashmap *m, KEY_TYPE *out_keys);

/* --- 扩展 --- */

/* 预留至少能容纳 n 个元素的容量(不足才扩容,幂次向上取)。
 * 成功返回 0;内存不足返回 -1(见 hashmap_last_error)。 */
int hashmap_reserve(hashmap *m, size_t n);

/* --- 错误处理 --- */

/* 返回上一次可能失败的操作的状态码(HASHMAP_OK / OOM / FULL / NOTFOUND)。 */
int hashmap_last_error(const hashmap *m);

/* ======================= KV 适配层(当前工程使用) ======================= */

extern hashmap *hash;

int kvs_hash_protocol(char *msg, char *response);
int hash_split_command(char *input, char *output[]);

#endif /* HASHMAP_H */
