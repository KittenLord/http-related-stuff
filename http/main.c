#include "coil.c"

// Defining callbacks
CoilCallback(CoilCB_printCallback, {
    Mem content = Coil_GetContent(context);

    printf("Received content:\n");
    printf("%.*s\n", memPrintf(content));

    checkRet(Coil_StatusLine(context, 204));
    checkRet(Coil_NoContent(context));
    return true;
})

CoilCallback(CoilCB_printSegment, {
    String value = Coil_GetPathMatch(context, "segmentValue");

    checkRet(Coil_StatusLine(context, 200));
    checkRet(Coil_AddContent(context, value));
    return true;
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
