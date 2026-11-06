#ifndef COROUTINUE_H
#define COROUTINUE_H


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include "nty_coroutine.h"
#define CONN_BUF_SIZE 4096

typedef int (*PROTOCOL) (char* msg,char* response);

extern PROTOCOL protocol_handler;

int coroutinue_start(int port, PROTOCOL handler);




#endif