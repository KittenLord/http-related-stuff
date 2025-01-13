#include <stdio.h>
#include <stdlib.h>

#include <stream.h>
#include <alloc.h>
#include "json.c"

int main() {
    Allocate(u8, myVar, 5);
    // Free(myVar);

    PeekStream s = mkPeekStream(mkStreamStr(mstr0("    a")));
    JSON_parseValue(&s);

    printf("%d\n", *myVar);
    return 0;
}
