#ifndef ARRAY_H
#define ARRAY_H
struct kv_array_entry
{
    char* key;
    char* value;
};

#define ARRAY_LENTH 1024

extern struct kv_array_entry* kv_array;

int kv_array_create(struct kv_array_entry** kv_array);
int kv_array_distory(struct kv_array_entry* kv_array);
int kv_array_set(struct kv_array_entry* kv_array,char **msg,char* output);
int kv_array_get(struct kv_array_entry* kv_array,char **msg,char* output);
int kv_array_del(struct kv_array_entry* kv_array,char **msg,char* output);
int kv_array_mod(struct kv_array_entry* kv_array,char **msg,char* output);
int kv_array_exist(struct kv_array_entry* kv_array,char **msg,char* output);
int split_command(char* input,char* output[]) ;//返回值为解析出来的单词数目
int kvs_array_protocol(char* msg,char* response);



#endif