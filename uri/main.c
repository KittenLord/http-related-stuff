#include <stdio.h>
#include <stdlib.h>

#include <stream.h>
#include <alloc.h>
#include "uri.c"

int main() {
    PeekStream s = mkPeekStream(mkStreamStr(mkString("https://%aatESt.test:8080/%aa/a/a/a")));

    Uri uri;
    Alloc resultAlloc = mkAlloc_LinearExpandable();
    UseAlloc(mkAlloc_LinearExpandable(), {
        uri = Uri_parseUri(&s, &resultAlloc);
    });

    if(uri.error) {
        printf("%s\n", uri.errmsg.s);
    }
    else {
        printf("Very good\n");

        printf("%s\n", uri.scheme.s);
        printf("%s\n", uri.hierarchyPart.authority.host.regName.s);
        printf("%d\n", uri.hierarchyPart.authority.port);
    }

    printf("Hello, World!\n");

    return 0;
}
