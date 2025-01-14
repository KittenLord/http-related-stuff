#include <stdio.h>
#include <stdlib.h>

#include <stream.h>
#include <alloc.h>
#include "json.c"

int main() {
    PeekStream s = mkPeekStream(mkStreamStr(mkString(" {\"a\":null,\"test\":{\"array\":[\"a\",\"b\",\"c\"]}} ")));

    JsonValue value;
    Alloc resultAlloc = mkAlloc_LinearExpandable();
    UseAlloc(mkAlloc_LinearExpandable(), {
        value = JSON_parse(&s, &resultAlloc);
    });

    Stream cout = mkStreamFd(STDOUT_FILENO);
    JSON_serialize(value, &cout, true);

    return 0;
}
