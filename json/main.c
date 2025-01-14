#include <stdio.h>
#include <stdlib.h>

#include <stream.h>
#include <alloc.h>
#include "json.c"

int main() {

    PeekStream s = mkPeekStream(mkStreamStr(mkString("  [\"aboba\"]  ")));

    JsonValue value;
    Alloc resultAlloc = mkAlloc_LinearExpandable();
    UseAlloc(mkAlloc_LinearExpandable(), {
        value = JSON_parseValue(&s, &resultAlloc);
    });

    printf("%x\n", value.error);
    if(value.errmsg.s) printf("%s\n", value.errmsg.s);
    printf("%x\n", value.type);

    return 0;
}
