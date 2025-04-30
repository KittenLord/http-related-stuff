#include <stdlib.h>
#include <stdio.h>

#include "deflate.c"
#include "gzip.c"

int main() {
    u8 testData[] = {
        // 0xca, 0x48, 0xcd, 0xc9, 0xc9, 0x57, 0xc8, 0x40, 0x27, 0x01,

        0x1d, 0xc6, 0x49, 0x01, 0x00, 0x00,
        0x10, 0x40, 0xc0, 0xac, 0xa3, 0x7f, 0x88, 0x3d,
        0x3c, 0x20, 0x2a, 0x97, 0x9d, 0x37, 0x5e, 0x1d,
        0x0c,

        // 0x4b, 0x4c, 0x4a, 0x4c, 0x4e, 0x4c,
        // 0x49, 0x4c, 0x4d, 0x4c, 0x4b, 0x4c, 0x07, 0x00
    };

    FILE *file = fopen("./deflate.c", "r");
    int fd = fileno(file);
    Stream s = mkStreamFd(fd);
    StringBuilder sb = mkStringBuilder();
    MaybeChar c;
    while(isJust(c = stream_popChar(&s))) {
        sb_appendChar(&sb, c.value);
    }
    
    Mem raw = sb_build(sb);

    Mem compressed = Deflate_compress(raw, false, 0, ALLOC);


    Mem decompressed = Deflate_decompress(compressed, ALLOC);
    write(STDOUT_FILENO, raw.s, raw.len);
    write(STDOUT_FILENO, decompressed.s, decompressed.len);
    printf("raw len %d\n", raw.len);
    printf("compressed len %d\n", compressed.len);
    printf("decompressed len %d\n", decompressed.len);

    return 0;
}
