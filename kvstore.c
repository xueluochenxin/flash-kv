#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#define NETWORK_REACTOR 0
#define NETWORK_PROACTOR 1
#define NETWORK_NTYCO 2
#define NETWORK_SELECT NETWORK_REACTOR

#include "reactor.h"

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
int kvs_protocol(char** msg,int lenth,char* response)
{
	char* command=msg[0];
    if(strcmp(command,"SET")==0)
	{

	}
    if(strcmp(command,"GET")==0)
	{

	}
    if(strcmp(command,"DEL")==0)
	{
		
	}
    if(strcmp(command,"MOD")==0)
	{
		
	}
	if(strcmp(command,"EXIST")==0)
	{
		
	}		
}
int main(int argc,char** argv)
{
    if (argc != 2) return -1;

	int port = atoi(argv[1]);

#if (NETWORK_SELECT == NETWORK_REACTOR)
	reactor_start(port, kvs_protocol);  
#elif (NETWORK_SELECT == NETWORK_PROACTOR)
	ntyco_start(port, kvs_protocol);
#elif (NETWORK_SELECT == NETWORK_NTYCO)
	proactor_start(port, kvs_protocol);
#endif

}