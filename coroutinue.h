#ifndef COROUTINUE_H
#define COROUTINUE_H


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include "nty_coroutine.h"


typedef int (*PROTOCOL) (char* msg,char* response);

extern PROTOCOL protocol_handler;

static void deal_conn(void *arg);
static void init_server(void *arg);
int coroutinue_start(int port, PROTOCOL handler);




#endif