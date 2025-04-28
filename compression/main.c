#include <stdlib.h>
#include <stdio.h>

#include "deflate.c"
#include "gzip.c"

int main() {
    u8 testData[] = {
        0xcb, 0x48, 0xcd, 0xc9, 0xc9, 0x57, 0xc8, 0x40, 0x27, 0x01
    };

    Mem mem = Deflate_decompress(mkMem(testData, 10), ALLOC);
    printf("%s\n", mem.s);

    return 0;
}
