#include "coil.c"
#include "logging.c"

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
    Log_message(LOG_INFO, mkString("helo"));
    Log_message(LOG_WARNING, mkString("helo"));
    Log_message(LOG_ERROR, mkString("helo"));

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
