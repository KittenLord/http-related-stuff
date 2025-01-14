#include <stdio.h>
#include <stdlib.h>

#include <stream.h>
#include <alloc.h>
#include "json.c"

int main() {
    PeekStream s = mkPeekStream(mkStreamStr(mkString("-9223372036854775807")));

    JsonValue value;
    Alloc resultAlloc = mkAlloc_LinearExpandable();
    UseAlloc(mkAlloc_LinearExpandable(), {
        value = JSON_parse(&s, &resultAlloc);
    });

    Stream cout = mkStreamFd(STDOUT_FILENO);
    JSON_serialize(value, &cout, true);

    return 0;
}
