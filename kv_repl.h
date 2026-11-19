#ifndef KV_REPL_H
#define KV_REPL_H

#include <stddef.h>
 
//zhu
/* 注册 SYNC 钩子。只有主节点调 */
int kv_repl_master_init(void);

/* 广播一条写命令给所有从节点（由 AOF 钩子调用） */
void kv_repl_broadcast(const char *raw, size_t len);

/* 连接关闭时调用：把 fd 从从节点表里移除。
   为什么必须调：fd 会被操作系统复用，表里留着旧 fd 会把命令发给无关客户端。 */
void kv_repl_on_close(int fd);


//cong
/* 启动同步协程：连主节点 → 拉全量 → 持续重放。
   必须在本进程的调度器（coroutinue_start）启动之前调用。 */
int kv_repl_slave_start(const char *master_ip, int master_port,
                        int (*handler)(char *msg, char *response));

/* 本进程是不是从节点。protocol() 用它做只读保护 */
int kv_repl_is_slave(void);


int kv_repl_is_slave_fd(int fd);
#endif /* KV_REPL_H */