#ifndef SKIPLIST_H
#define SKIPLIST_H

/* ============================================================================
 * skiplist.h —— 跳表(Skip List)库
 *
 * 设计要点:
 *   - 键:   char*(NUL 结尾字符串)
 *   - 值:   char*(NUL 结尾字符串)
 *   - 所有权: skiplist_insert 会复制 key/value；
 *             skiplist_erase / skiplist_clear / skiplist_destroy
 *             会自动释放表内部的 key/value。
 *             调用方传给 insert 的字符串在 insert 返回后可以自行释放。
 *   - 读取: skiplist_get / skiplist_foreach / skiplist_range
 *           返回内部指针，只读，不要 free；
 *           在下一次 insert/erase/clear/destroy 前有效。
 *   - 层数: 期望最高 32 层,每层晋升概率 1/2
 *   - 结构: 带头结点(不存 key) + NULL 哨兵尾
 *   - 排序: key 按 strcmp 从小到大;重复 key 视为更新
 *
 * 用法示例:
 *   skiplist *s = skiplist_create(0);
 *   skiplist_insert(s, "name", "flash");   // 表内部复制 key/value
 *   char *got = skiplist_get(s, "name");    // 返回内部指针，只读
 *   skiplist_erase(s, "name");             // 自动释放内部 key/value
 *   skiplist_destroy(s);                   // 自动释放所有剩余 key/value
 * ==========================================================================*/

#include <stddef.h>

/* ---------- 键类型固定为 char*;可在 include 前覆盖,但必须兼容 strcmp ---------- */
#ifndef KEY_TYPE
#define KEY_TYPE char *
#endif

/* 表状态:供 skiplist_last_error() 返回 */
#define SKIPLIST_OK            0  /* 上次操作成功 */
#define SKIPLIST_ERR_OOM       1  /* 内存分配失败 */
#define SKIPLIST_ERR_RANGE     2  /* 区间参数非法(strcmp(low, high) > 0) */
#define SKIPLIST_ERR_NOTFOUND  3  /* 键不存在 */
#define SKIPLIST_ERR_PARAM     4  /* 参数无效(NULL 或 p 越界) */

/* 晋升概率 p 的定点数表示:p_q5 = p * 32(p ∈ [1,32])。
 * 默认 SKIPLIST_P_Q5_DEFAULT = 16,即 p = 0.5。 */
#define SKIPLIST_P_Q5_DEFAULT 16
#define SKIPLIST_MAX_LEVEL     32   /* 最大层数上限 */

/* 遍历/区间回调:key/value 都是表内部的 char*;返回 0 继续,非 0 提前终止。
 * 不要 free 这两个指针。 */
typedef int (*skiplist_iter_fn)(KEY_TYPE key, char *value, void *userdata);

/* 有序节点(头结点的 key/value 恒为 NULL,勿使用) */
typedef struct skiplist_node {
    KEY_TYPE key;                    /* char* 字符串键,由跳表管理 */
    char    *value;                  /* char* 字符串值,由跳表管理 */
    struct skiplist_node **forward;  /* forward[i] = 第 i 层的下一个节点 */
    int      level;                  /* 该节点实际拥有的层数 */
} skiplist_node;

/* 对外暴露的跳表结构体(字段视为内部实现,请勿直接修改) */
typedef struct skiplist {
    skiplist_node *head;             /* 头结点:永远存在,不存数据 */
    int            max_level;        /* 当前允许的最大层数 */
    int            cur_level;        /* 目前实际用到的最高层(0 起) */
    size_t         size;             /* 元素个数 */
    int            last_error;
    double         p;                /* 晋升概率,默认 0.5 */
} skiplist;

/* ======================= 对外接口 ======================= */

/* --- 创建 / 销毁 --- */

/* 创建跳表。max_level 传 0 使用默认 SKIPLIST_MAX_LEVEL(32);
 * 传入值会被夹到 [1, SKIPLIST_MAX_LEVEL]。失败返回 NULL。 */
skiplist *skiplist_create(int max_level);

/* 销毁整张表。
 * 对每个仍然存在的 key/value 调用 kv_free,然后释放节点、头结点和表本身。
 * 销毁后指针不可再使用。 */
void skiplist_destroy(skiplist *s);

/* 清空所有节点(保留结构,可继续插入)。
 * 对每个仍然存在的 key/value 调用 kv_free。 */
void skiplist_clear(skiplist *s);

/* --- 增 / 删 / 改 --- */

/* 插入 (key, value)。
 * 本函数会复制 key/value,调用方不需要保留传入的字符串。
 * value 不能为 NULL。
 * 返回 1:插入成功
 * 返回 0:key 已存在,更新了 value(旧 value 已自动释放)
 * 返回 -1:参数非法或内存分配失败(见 skiplist_last_error) */
int skiplist_insert(skiplist *s, KEY_TYPE key, char *value);

/* 删除 key。
 * 成功返回 1,并自动释放表内部的 key/value。
 * key 不存在返回 0。 */
int skiplist_erase(skiplist *s, KEY_TYPE key);

/* --- 查 / 读 --- */

/* 取 key 对应的值;不存在返回 NULL。
 * 返回的是表内部指针,只读,不要 free；
 * 在下一次 insert/erase/clear/destroy 前有效。 */
char *skiplist_get(skiplist *s, KEY_TYPE key);

/* key 是否存在,返回 1 / 0。 */
int skiplist_contains(const skiplist *s, KEY_TYPE key);

/* 元素个数。 */
size_t skiplist_size(const skiplist *s);

/* 空表返回 1。 */
int skiplist_empty(const skiplist *s);

/* --- 有序范围(跳表相对普通哈希表的核心优势) --- */

/* 返回 key 最小的节点;空表返回 NULL。头结点不算节点。
 * 返回的是内部节点,不要 free。 */
skiplist_node *skiplist_first(const skiplist *s);

/* 返回严格大于 node->key 的下一个节点;越界返回 NULL。
 * 遍历期间禁止插入/删除(会使 node 悬空)。 */
skiplist_node *skiplist_next(const skiplist *s, skiplist_node *node);

/* 区间扫描 [low, high] 闭区间,按 key 升序回调 fn。
 * fn 返回非 0 提前终止,并作为返回值传出。
 * strcmp(low, high) > 0 返回 SKIPLIST_ERR_RANGE。
 * 返回 0 表示全部回调完成。
 * 回调里的 key/value 是内部指针,不要 free。 */
int skiplist_range(skiplist *s, KEY_TYPE low, KEY_TYPE high,
                   skiplist_iter_fn fn, void *userdata);

/* --- 遍历 --- */

/* 按 key 升序对每个 (key, value) 调用 fn;fn 返回非 0 提前结束。
 * key/value 是内部指针,不要 free。
 * 遍历期间禁止 insert/erase/clear/destroy。 */
int skiplist_foreach(const skiplist *s, skiplist_iter_fn fn, void *userdata);

/* --- 扩展 --- */

/* 修改晋升概率 p(q5 = p*32,范围 [1,32])。只影响之后新节点。
 * 成功 0,参数越界 -1。 */
int skiplist_set_p(skiplist *s, int p_q5);

/* --- 错误处理 --- */

/* 返回上一次操作的状态码(SKIPLIST_OK / OOM / RANGE / NOTFOUND / PARAM)。 */
int skiplist_last_error(const skiplist *s);


//
int skiplist_split_command(char *input, char *output[]); // 返回值为解析出来的单词数目
int kvs_skiplist_protocol(char *msg, char *response);
extern skiplist* sl;
#endif 
/* SKIPLIST_H */
