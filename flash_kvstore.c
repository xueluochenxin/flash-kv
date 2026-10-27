#define REACTOR 1
#define PROACTOR 2
#define NTYCO 3

#define ARRAY_S 1
#define RBTREE_S 2
#define HASH_S 3
#define SKIPLIST_S 4


#define NET_ENGINE PROACTOR
#define STORE_ENGINE SKIPLIST_S


#if NET_ENGINE == REACTOR

#include "reactor.h"

#elif NET_ENGINE == PROACTOR

#include "proactor.h"

#endif

#if STORE_ENGINE == ARRAY_S
#include "array.h"

#elif STORE_ENGINE == RBTREE_S
#include "RBTREE.h"

#elif STORE_ENGINE == HASH_S 
#include "hashmap.h"

#elif STORE_ENGINE == SKIPLIST_S
#include "skiplist.h"

#endif

#ifndef KV_FREE
#include "kvmemctl.h"
#endif
int protocol(char* msg,char* response)
{


#if (STORE_ENGINE == ARRAY_S)
return kvs_array_protocol(msg,response);
#elif (STORE_ENGINE == RBTREE_S)
return kvs_rbtree_protocol(msg,response);
#elif (STORE_ENGINE == HASH_S)
return kvs_hash_protocol(msg,response);
#elif (STORE_ENGINE == SKIPLIST_S)
return kvs_skiplist_protocol(msg,response);
#endif


}


int main(int argc,char **argv)
{
   if(argc!=2)
   {
    return -1;
   }
   int port = atoi(argv[1]);
   
#if STORE_ENGINE == ARRAY_S
    kv_array_create(&kv_array);
#elif STORE_ENGINE == RBTREE_S
    T=RBTREE_create();
#elif (STORE_ENGINE == HASH_S)
hash=hashmap_create(0);
#elif (STORE_ENGINE == SKIPLIST_S)
sl=skiplist_create(0);
#endif



#if(NET_ENGINE == REACTOR)

reactor_start(port,protocol);
#elif (NET_ENGINE == PROACTOR)
proactor_start(port,protocol);
#endif








#if (STORE_ENGINE == ARRAY_S)
    kv_array_distory(kv_array);
#elif (STORE_ENGINE == RBTREE_S)
    RBTREE_destroy(T);
#elif (STORE_ENGINE == HASH_S)
hashmap_destroy(hash);
#elif (STORE_ENGINE == SKIPLIST_S)
skiplist_destroy(sl);
#endif
}