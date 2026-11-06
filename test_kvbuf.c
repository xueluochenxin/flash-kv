#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kvbuf.h"

int main(void)
{
    kvbuf b;
    kvbuf_init(&b);

    /* ── 测试 1：逐字节追加，观察 cap 怎么翻倍 ── */
    printf("=== 测试1：一个字节一个字节地塞 ===\n");
    for (int i = 0; i < 1000; i++) {
        kvbuf_append(&b, "x", 1);
        if (i == 0 || i == 255 || i == 256 || i == 511 || i == 512 || i == 999)
            printf("  塞了 %4d 次  ->  len=%4zu  cap=%4zu\n", i + 1, b.len, b.cap);
    }

    /* ── 测试 2：一次性塞 1MB，看是否精确分配 ── */
    printf("\n=== 测试2：一次塞 1MB ===\n");
    kvbuf_reset(&b);

    size_t big = 1024 * 1024;
    char *src = malloc(big);
    memset(src, 'A', big);

    int rc = kvbuf_append(&b, src, big);
    printf("  追加结果 rc=%d  len=%zu  cap=%zu\n", rc, b.len, b.cap);

    /* ── 测试 3：空缓冲的初始状态 ── */
    printf("\n=== 测试3：空缓冲 ===\n");
    kvbuf e;
    kvbuf_init(&e);
    printf("  p=%p  len=%zu  cap=%zu\n", (void *)e.p, e.len, e.cap);

    kvbuf_destroy(&b);
    kvbuf_destroy(&e);
    free(src);
    return 0;
}
