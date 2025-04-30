#include <stdlib.h>
#include <stdio.h>

#include "deflate.c"
#include "gzip.c"
#include "zlib.c"

int main() {
    FILE *file = fopen("./deflate.c", "r");
    int fd = fileno(file);
    Stream s = mkStreamFd(fd);
    StringBuilder sb = mkStringBuilder();
    MaybeChar c;
    while(isJust(c = stream_popChar(&s))) {
        sb_appendChar(&sb, c.value);
    }
    Mem raw = sb_build(sb);

    Mem compressed = Zlib_compress(raw, ALLOC);
    write(STDOUT_FILENO, compressed.s, compressed.len);

    // Mem decompressed = Zlib_decompress(raw, ALLOC);
    // printf("%p\n", decompressed.s);
    // write(STDOUT_FILENO, decompressed.s, decompressed.len);

    return 0;
}
