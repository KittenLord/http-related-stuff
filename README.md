> [!WARNING]
> This library is far from being done - source code is not
  organized, API has not yet been finalized. That's not even
  mentioning that it's written in C, memory issues are inevitable.
> Thus, use at your own risk.

# A simple way to set up an HTTP server

Inspired by my soul-sucking experience with Spring Boot, I decided
to try my hand at making an HTTP library which doesn't try its best
at hiding every interesting detail from its users.

As this is more of an educational project for myself, this library
does not (and will not) have any dependencies, apart from libc.

This library tries to rely on libc and traditional C semantics as
little as possible. When using this library, you're expected to use
its primitives instead of C ones. This means:

- Using `Mem` or `String` instead of null-terminated strings
- Using allocator API instead of calling `malloc`
    - For a function `doWork(Result *result, Alloc *alloc)`
      the `result` is allocated in `alloc`, and `ALLOC` is
      being used as a temporary allocator - use
      `UseAlloc(a, block)` for assigning `ALLOC`
- Using `stream` API for parsing purposes
- Using `Dynar(a)` for dynamic array functionality
- Using `rune` whenever UTF-8 handling is needed
- Using `i8`/`u8`/`usz`/etc
- Using `pure(result)`/`cont(result)` for chaining operations
  (though truth be told, the amount of jumping they cause is
  probably far from optimal, so this is very optional)

There'll hopefully be an actual documentation eventually

# Example setup

```c
#include <coil.c>

// Defining callbacks
CoilCallback(CoilCB_printCallback, {
    Mem content = Coil_GetContent(context);

    printf("Received content:\n");
    printf("%.*s\n", memPrintf(content));

    pure(result) Coil_StatusLine(context, 204);
    cont(result) Coil_NoContent(context);
    return result;
})

CoilCallback(CoilCB_printSegment, {
    String value = Coil_GetPathMatch(context, "segmentValue");

    pure(result) Coil_StatusLine(context, 200);
    cont(result) Coil_AddContent(context, value);
    return result;
})

int main(void) {
    MaybeSocket sockM = Coil_GetSocket(6969, 128);
    if(isNone(sockM)) return 1;
    int sock = sockM.value;

    FileStorage storage = mkFileStorage(ALLOC_GLOBAL);
    Router router = mkRouter();
    FileTreeRouter ftrouter = mkFileTreeRouter("./dir", &storage);

    AddRoutePtr(&router, GET,  "/files/*",                CoilCB_fileTree, FileTreeRouter, &ftrouter);
    AddRouteStr(&router, GET,  "/test",                   CoilCB_data, "<body><h1>Test!</h1></body>");
    AddRouteNil(&router, POST, "/print",                  CoilCB_printCallback);
    AddRouteNil(&router, ANY,  "/segment/{segmentValue}", CoilCB_printSegment);

    bool result = Coil_Run(sock, &router);
    return result ? 0 : 1;
}
```
