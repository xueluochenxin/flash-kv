#ifndef _RBTREE_H_
#define _RBTREE_H_

#include <stdio.h>
#include <stdlib.h>

typedef unsigned char color_t;
#define RED   ((color_t)1)
#define BLACK ((color_t)2)

/* 本工程统一使用 NUL 结尾的字符串键/值。
 * 红黑树只保存 char* 指针,不 strdup、不 free key/value;
 * 节点的 key/value 生命周期由调用方管理。
 * 删除/销毁后库不再保存节点指针,调用方应保留自己插入时的 key/value 指针用于释放。 */
#ifndef KEY_TYPE
#define KEY_TYPE char *
#endif

/* 让节点结构可以嵌入任意业务结构体(侵入式设计) */
#define RBTREE_ENTRY(name, type) \
struct name {                    \
    struct type *right;          \
    struct type *left;           \
    struct type *parent;         \
    color_t      color;          \
}

/* 红黑树节点结构 */
typedef struct rbtree_node {
    KEY_TYPE key;      /* char* 字符串键 */
    char    *value;    /* char* 字符串值,由调用方管理 */
    RBTREE_ENTRY(, rbtree_node) node;
} rbtree_node;

/* 红黑树根节点与哨兵结构 */
typedef struct rbtree {
    struct rbtree_node *root;
    struct rbtree_node *nil; /* 哨兵:颜色为黑,left/right 指向自身,parent 为 NULL */
} rbtree;

/* ==================== 公有 API 接口 ==================== */

//变量
extern rbtree* T;

/* 创建与销毁 */
rbtree *RBTREE_create(void);
void    RBTREE_destroy(rbtree *T);   /* 只释放节点/哨兵/树,不释放 key/value */

/* 查找:返回找到的节点指针,没找到返回 NULL */
rbtree_node *RBTREE_search(rbtree *T, KEY_TYPE key);

/* 插入:
 * 注意:传进来的 z 必须是 malloc 出来的,key/value 必须是 char* 字符串,
 *       且在外部填好。本库不拷贝、不释放它们。
 * 返回值:0 成功,-1 表示参数非法或 key 已存在(此时节点未插入,外部需自行释放 z)。 */
int RBTREE_insert(rbtree *T, rbtree_node *z);

/* 删除:
 * 注意:此函数只会 free 节点本身,不会 free(z->key)、也不会 free(z->value)。
 * 返回值:0 成功,-1 表示 key 不存在。 */
int RBTREE_delete(rbtree *T, KEY_TYPE key);

/* 校验与打印(用于测试) */
int  RBTREE_check(rbtree *T);        /* 返回 1 合法,0 非法 */
void RBTREE_inorder(rbtree *T, rbtree_node *node); /* 中序遍历打印 */

int kvs_rbtree_protocol(char* msg,char* response);
int rb_split_command(char* input,char* output[]); //返回值为解析出来的单词数目

#endif /* _RBTREE_H_ */