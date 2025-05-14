
#include "coil.c"

#include <stdio.h>
#include <stdlib.h>

// https://askubuntu.com/a/1471201
//
// Reference command:
// systemd-run --scope -p MemoryMax=5M --user ../bin/http-testing

// NOTE: currently the only thing that might be bad (apart from
// all the places where there is no error checking where it
// should be) is a lot of excessive allocations where they're
// just not needed. Each request has its own dedicated allocator,
// so it's not like the functions returning memory need to care
// about cloning memory received from that same allocator, but
// at the same time making functions refer to the current
// allocator is bad for composability (input might not always
// be in the same allocator, obviously). Will think what the best
// solution is (if there needs to be one, since I'm using linear
// allocators the only thing that impactfully wastes work is
// mem_copy)

int main(int argc, char **argv) {
    // NOTE: if client dies, we likely encounter an error on
    // our next read() in threadRoutine. We then goto cleanup and
    // try to flush the stream via write(), causing SIGPIPE
    // Not sure why this didn't occur before though, is the parsing
    // getting too slow?
    signal(SIGPIPE, SIG_IGN);

    argc = argc;
    argv = argv;

    ALLOC_PUSH(mkAlloc_LinearExpandable());

    MaybeSocket sockM = Coil_GetSocket(6969, 128);
    if(isNone(sockM)) return 1;
    int sock = sockM.value;

    Router router = mkRouter();
    FileStorage storage = mkFileStorage(ALLOC_GLOBAL);
    FileTreeRouter ftrouter = mkFileTreeRouter(mkString("./dir"), &storage);

    AddRouteArg(&router, GET, "/files/*", fileTreeCallback, memPointer(FileTreeRouter, &ftrouter));
    AddRouteArg(&router, GET, "/test", dataCallback, mkString("<body><h1>Test!</h1></body>"));
    AddRoute(&router, POST, "/print", printCallback);

    Coil_Start(sock, &router);

    int closeResult = close(sock);
    printf("CLOSE %d\n", closeResult);

    ALLOC_POP();

    return 0;
}
