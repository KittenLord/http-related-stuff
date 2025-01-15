#include <stdio.h>
#include <stdlib.h>

#include <stream.h>
#include <alloc.h>
#include "uri.c"

int main() {
    PeekStream s = mkPeekStream(mkStreamStr(mkString("https://test.test/a?query#fragment")));

    // Uri uri;
    // Alloc resultAlloc = mkAlloc_LinearExpandable();
    // UseAlloc(mkAlloc_LinearExpandable(), {
    //     uri = URI_parse(&s, &resultAlloc);
    // });

    printf("Hello, World!\n");

    return 0;
}
