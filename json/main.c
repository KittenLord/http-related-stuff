#include <stdio.h>
#include <stdlib.h>

#include <stream.h>
#include <alloc.h>
#include "json.c"

int main() {

    PeekStream s = mkPeekStream(mkStreamStr(mkString("\"hello\"")));
    JsonValue value = JSON_parseValue(&s);

    printf("%d\n", value.type);
    printf("%s\n", value.string.s);

    return 0;
}
