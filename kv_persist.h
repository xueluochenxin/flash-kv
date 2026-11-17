#ifndef KV_PERSIST_H
#define KV_PERSIST_H

#include <stddef.h>


#define DATA_DIR  "./data"
#define SNAPSHOT  DATA_DIR "/snapshot.aof"
#define AOF_FILE  DATA_DIR "/appendonly.aof"

/* 全量：遍历内存 → 写快照（tmp + fsync + rename 原子替换） */
int kv_dump(const char *path);

/* 把命令流文件回放进内存。快照和 AOF 共用这一个函数 */
int kv_load(const char *path, int (*handler)(char *msg, char *response));

/* 打开 AOF 并注册钩子。必须在所有 kv_load 之后调用 */
int kv_aof_open(const char *path);

/* 清空 AOF，重新进入追加状态（SAVE 成功后调用） */
int kv_aof_truncate(void);

#endif /* KV_PERSIST_H */