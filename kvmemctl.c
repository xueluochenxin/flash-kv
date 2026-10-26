#include "kvmemctl.h"

#include <stdlib.h>

void *kv_malloc(size_t size)
{
    return malloc(size);
}

void kv_free(void *ptr)
{
    free(ptr);
}
