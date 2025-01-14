#include <stdio.h>
#include <stdlib.h>

#include <stream.h>
#include <alloc.h>
#include "json.c"

int main() {

    PeekStream s = mkPeekStream(mkStreamStr(mkString(" { \"a\":\"b\",\"c\":{\"inner\":\"i\"} } ")));

    JsonValue value;
    Alloc resultAlloc = mkAlloc_LinearExpandable();
    UseAlloc(mkAlloc_LinearExpandable(), {
        value = JSON_parseValue(&s, &resultAlloc);
    });

    printf("%x\n", value.error);
    if(value.errmsg.s) printf("%s\n", value.errmsg.s);
    printf("col:row %d:%d\n", s.s.col, s.s.row);
    printf("%x\n", value.type);

    Stream cout = mkStreamFd(STDOUT_FILENO);
    JSON_serialize(value, &cout, true);

    return 0;
}
