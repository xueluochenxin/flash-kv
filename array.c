#include "array.h"
#include "kvmemctl.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
struct kv_array_entry* kv_array=NULL;



int kv_array_create(struct kv_array_entry** kv_array)
{
*kv_array=kv_malloc(sizeof(struct kv_array_entry)*ARRAY_LENTH);
if(*kv_array==NULL)
{
    return -1;
}
memset(*kv_array,0,sizeof(struct kv_array_entry)*ARRAY_LENTH);
return 1;
}

int kv_array_distory(struct kv_array_entry* kv_array)
{
    for(int i=0;i<ARRAY_LENTH;++i)
    {
        if(kv_array[i].key!=NULL)
        {
            kv_free(kv_array[i].key);
        }
        if(kv_array[i].value!=NULL)
        {
            kv_free(kv_array[i].value);
        }
    }    
kv_free(kv_array);
return 1;
}

int kv_array_set(struct kv_array_entry* kv_array,char **msg,char* output)
{
int index=0;
int issucces=0;
while(index<ARRAY_LENTH)
{
    if(kv_array[index].key==NULL)
    {
        issucces=1;
        break;        
    }
    index++;
}
if(issucces==0)
{
    //printf("array is full\n");
    sprintf(output,"WRONG\n");
    return -1; 
}
    kv_array[index].key   = kv_malloc(strlen(msg[1]) + 1);  
    kv_array[index].value = kv_malloc(strlen(msg[2]) + 1);
    strcpy(kv_array[index].key, msg[1]);                     
    strcpy(kv_array[index].value, msg[2]);
sprintf(output,"OK\n");
    return 1;
}

int kv_array_get(struct kv_array_entry* kv_array,char **msg,char* output)
{
int index=0;
int success=0;
while(index<ARRAY_LENTH)
{
    if(kv_array[index].key != NULL &&strcmp(kv_array[index].key,msg[1])==0)
    {
        sprintf(output,"%s\n",kv_array[index].value);
        success=1;
        break;
    }
    index++;
}
if(success==1)
{
    return 1;
}
if(success==0)
{
    sprintf(output,"INEXIST\n");
    return -1;
}
}

int kv_array_del(struct kv_array_entry* kv_array,char **msg,char* output)
{
int index=0;
int success=0;
while(index<ARRAY_LENTH)
{
    if(kv_array[index].key != NULL &&strcmp(kv_array[index].key,msg[1])==0)
    {
        sprintf(output,"OK\n");
        kv_free(kv_array[index].key);
        kv_free(kv_array[index].value);
        kv_array[index].key=NULL;
        kv_array[index].value=NULL;
        success=1;
        break;
    }
    index++;
}
if(success)
{
    return 1;
}
if(success==0)
{
    sprintf(output,"INEXIST\n");
    return -1;
}
}


int kv_array_mod(struct kv_array_entry* kv_array,char **msg,char* output)
{
int index=0;
int success=0;
while(index<ARRAY_LENTH)
{
    if(kv_array[index].key != NULL &&strcmp(kv_array[index].key,msg[1])==0)
    {
        sprintf(output,"OK\n");
        kv_free(kv_array[index].value);
        kv_array[index].value = kv_malloc(strlen(msg[2]) + 1);
        strcpy(kv_array[index].value, msg[2]);
        success=1;
        break;
    }
    index++;
}
if(success==1)
{
    return 1;
}
if(success==0)
{
    sprintf(output,"INEXIST\n");
    return -1;
}
}

int kv_array_exist(struct kv_array_entry* kv_array,char **msg,char* output)
{
int index=0;
int success=0;
while(index<ARRAY_LENTH)
{
    if(kv_array[index].key != NULL &&strcmp(kv_array[index].key,msg[1])==0)
    {
        sprintf(output,"YES\n");
        success=1;
        break;
    }
    index++;
}
if(success==1)
{
    return 1;
}
if(success==0)
{
    sprintf(output,"NO\n");
    return -1;
}
}

int split_command(char* input,char* output[]) //返回值为解析出来的单词数目
{
    int count = 0;
    char *p = input;

    while (*p) {
        /* 1. 跳过所有空白 */
        while (*p && isspace((unsigned char)*p))
            p++;

        /* 2. 到达字符串末尾，结束 */
        if (*p == '\0')
            break;

        /* 3. 记录这个单词的开始 */
        output[count++] = p;

        /* 4. 向后扫描直到遇到空白 */
        while (*p!='\0' && !isspace((unsigned char)*p))
            p++;

        /* 5. 把分隔符替换成 '\0'，以便单词独立成串 */
        if (*p) {
            *p = '\0';
            p++;              /* 跳到下一个位置继续找 */
        }
    }

    return count;
}
int kvs_array_protocol(char* msg,char* response)
{
    char *tokens[16]={0};
    int count=split_command(msg,tokens);
        if(count == 0||tokens[0]==NULL)          // 空输入保护
    {
        sprintf(response,"ERROR\n");
        return -1;
    }
	char* command=tokens[0];

    if(strcmp(command,"SET")==0)
	{
        kv_array_set(kv_array,tokens,response);
	}
    if(strcmp(command,"GET")==0)
	{
       kv_array_get(kv_array,tokens,response);
	}
    if(strcmp(command,"DEL")==0)
	{
		kv_array_del(kv_array,tokens,response);
	}
    if(strcmp(command,"MOD")==0)
	{
		kv_array_mod(kv_array,tokens,response);
	}
	if(strcmp(command,"EXIST")==0)
	{
		kv_array_exist(kv_array,tokens,response);
	}
    return strlen(response);
}