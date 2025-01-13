#include <stdio.h>
#include <stdlib.h>

#include <stream.h>
#include <alloc.h>
#include "json.c"

int main() {
    PeekStream s = mkPeekStream(mkStreamStr(mstr0("    a")));
    JSON_parseValue(&s);

    printf("Hello!\n");
    return 0;
}
