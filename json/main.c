#include <stdio.h>
#include <stdlib.h>

#include <stream.h>
#include <alloc.h>

#include <http/json.c>

int main() {
    FILE *testJson = fopen("test.json", "r");
    int testJsonFd = fileno(testJson);

    Stream s = mkStreamFd(testJsonFd);

    JsonValue value;
    Alloc resultAlloc = mkAlloc_LinearExpandable();
    UseAlloc(mkAlloc_LinearExpandable(), {
        value = JSON_parse(&s, &resultAlloc);
    });

    Stream cout = mkStreamFd(STDOUT_FILENO);
    JSON_serialize(value, &cout, true);

    return 0;
}
