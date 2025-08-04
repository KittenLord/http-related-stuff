#ifndef __LIB_COIL
#define __LIB_COIL

#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <signal.h>
#include <pthread.h>
#include <errno.h>

#include "http.c"

#include "file.c"
#include "router.c"
#include "logging.c"

#ifndef CONTENT_LIMIT
#define CONTENT_LIMIT 100000000
#endif

#define Coil_GetPathMatch(c, s) Coil_GetPathMatchL((c), mkString(s))
String Coil_GetPathMatchL(RouteContext *context, String segment) {
    return map_get(context->matches, segment);
}

bool Coil_AddDate(RouteContext *context) {
    tryRet(stream_write(context->s, mkString("Date: ")));
    checkRet(Http_writeDateNow(context->s));
    checkRet(Http_writeCRLF(context->s));
    return true;
}

bool Coil_AddETag(RouteContext *context, Mem data, bool isWeak) {
    tryRet(stream_write(context->s, mkString("ETag: ")));
    if(isWeak) {
        checkRet(stream_writeChar(context->s, 'W'));
        checkRet(stream_writeChar(context->s, '/'));
    }

    checkRet(stream_writeChar(context->s, '\"'));
    Stream s = mkStreamStr(data);
    checkRet(writeBytesToBase64(&s, context->s, false, false));
    checkRet(stream_writeChar(context->s, '\"'));
    checkRet(Http_writeCRLF(context->s));
    return true;
}

bool Coil_AddLastModified(RouteContext *context, time_t lastModified) {
    tryRet(stream_write(context->s, mkString("Last-Modified: ")));
    checkRet(Http_writeDate(context->s, lastModified));
    checkRet(Http_writeCRLF(context->s));
    return true;
}

bool Coil_AddHeader(RouteContext *context, String header, String value) {
    tryRet(stream_write(context->s, header));
    checkRet(stream_writeChar(context->s, ':'));
    checkRet(stream_writeChar(context->s, ' '));
    tryRet(stream_write(context->s, value));
    checkRet(Http_writeCRLF(context->s));
    return true;
}

bool Coil_SealHeaders(RouteContext *context) {
    if(!context->sealedStatus) return false;
    if(context->sealedHeaders) return false;

    checkRet(Http_writeCRLF(context->s));

    context->sealedHeaders = true;
    return true;
}

bool Coil_AddContentLength(RouteContext *context, u64 length) {
    tryRet(stream_write(context->s, mkString("Content-Length: ")));
    checkRet(writeU64ToDecimal(context->s, length));
    checkRet(Http_writeCRLF(context->s));
    return true;
}

bool Coil_AddAllNecessaryHeaders(RouteContext *context) {
    checkRet(Coil_AddDate(context));
    checkRet(Coil_AddHeader(context, mkString("Connection"), mkString("keep-alive")));
    return true;
}

bool Coil_AddContent(RouteContext *context, Mem content) {
    checkRet(Coil_AddContentLength(context, content.len));
    checkRet(Coil_SealHeaders(context));
    if(context->method != HEAD) {
        tryRet(stream_write(context->s, content));
    }
    return true;
}

bool Coil_NoContent(RouteContext *context) {
    checkRet(Coil_SealHeaders(context));
    return true;
}

bool Coil_AddContentType(RouteContext *context, HttpMediaType mediaType) {
    tryRet(stream_write(context->s, mkString("Content-Type: ")));
    checkRet(Http_writeMediaType(context->s, mediaType));

    // TODO: make this less bad
    tryRet(stream_write(context->s, mkString(";charset=utf-8")));

    checkRet(Http_writeCRLF(context->s));
    return true;
}

bool Coil_AddFile(RouteContext *context, File file) {
    checkRet(Coil_AddContentType(context, file.mediaType));
    if(file.hasHash) {
        checkRet(Coil_AddETag(context, mkMem(file.hash.data, 256 / 8), false));
    }
    checkRet(Coil_AddLastModified(context, file.modTime));
    checkRet(Coil_AddContent(context, file.data));
    return true;
}

bool Coil_AddTransferEncoding(RouteContext *context, Dynar(HttpTransferCoding) *codings) {
    tryRet(stream_write(context->s, mkString("Transfer-Encoding: ")));
    dynar_foreach(HttpTransferCoding, codings) {
        if(loop.index != 0) {
            checkRet(stream_writeChar(context->s, ','));
            checkRet(stream_writeChar(context->s, ' '));
        }
        tryRet(stream_write(context->s, loop.it.coding));
    }
    checkRet(Http_writeCRLF(context->s));
    return true;
}

// NOTE: this would've been much cooler if we had lazy streams
// (i.e. we could make a nested stream that applies all
// codings, and then lazily request 1024 at a time from it)

// NOTE: also Firefox doesn't support any TE except for chunked,
// so I won't be able to test this lol
bool Coil_AddContentStream(RouteContext *context, Stream *s, Dynar(HttpTransferCoding) *codings) {
    if(context->clientVersion.value < Http_getVersion(1, 1)) {
        checkRet(Coil_SealHeaders(context));
        if(context->method != HEAD) {
            checkRet(stream_dumpInto(s, context->s, 0, true));
            context->persist = false;
        }
        return true;
    }

    Stream _s;

    dynar_foreach(HttpTransferCoding, codings) {
        if(
        // NOTE: chunked should never appear here
        !mem_eq(loop.it.coding, mkString("gzip")) &&
        !mem_eq(loop.it.coding, mkString("deflate")) &&
        true
        ) return false;

        if(!map_has(context->headers, mkString("te")) ||
           !dynar_containsString(&memExtractPtr(HttpH_TE, map_get(context->headers, mkString("te")))->codings, loop.it.coding)) {
            dynar_remove(HttpTransferCoding, codings, loop.index);
            loop.index -= 1;
        }
    }

    bool result;
    dynar_append(codings, HttpTransferCoding, mkHttpTransferCoding("chunked"), result);
    if(!result) return false;

    checkRet(Coil_AddTransferEncoding(context, codings));
    checkRet(Coil_SealHeaders(context));

    if(context->method == HEAD) return true;



    dynar_foreach(HttpTransferCoding, codings) {
        if(mem_eq(loop.it.coding, mkString("chunked"))) {
            ResultRead r;

            byte buffer[1024];

            while(isJust(r = stream_read(s, mkMem(buffer, 1024)))) {
                u64 len = r.read;
                if(len == 0) break;

                u64 elen = Sha_endian64(len);
                Stream elens = mkStreamStr(mkMem(&elen, 8));
                checkRet(writeBytesToHex(&elens, context->s, false, false));
                // here we would've put chunk extensions if we were sadists
                checkRet(Http_writeCRLF(context->s));
                tryRet(stream_write(context->s, mkMem(buffer, len)));
                checkRet(Http_writeCRLF(context->s));

                if(r.partial) break;
            }

            checkRet(stream_writeChar(context->s, '0'));
            checkRet(Http_writeCRLF(context->s));

            // here we would've put trailer fields if we were sadists

            checkRet(Http_writeCRLF(context->s));
        }
        else if(mem_eq(loop.it.coding, mkString("gzip"))) {
            Mem mem = Gzip_compress(stream_dump(s, ALLOC, 0, true), ALLOC);
            if(isNull(mem)) return false;
            _s = mkStreamStr(mem);
            s = &_s;
        }
        else if(mem_eq(loop.it.coding, mkString("deflate"))) {
            Mem mem = Zlib_compress(stream_dump(s, ALLOC, 0, true), ALLOC);
            if(isNull(mem)) return false;
            _s = mkStreamStr(mem);
            s = &_s;
        }
        else {
            return false;
        }
    }

    return true;
}

bool Coil_NotFound(RouteContext *context) {
    context->statusCode = 404;
    return Handle(context, context->lastRouter->handler_routeNotFound);
}

bool Coil_InternalError(RouteContext *context) {
    context->statusCode = 500;
    return Handle(context, context->lastRouter->handler_internalError);
}

bool Coil_StatusLine(RouteContext *context, HttpStatusCode statusCode) {
    checkRet(Http_writeStatusLine(context->s, 1, 1, statusCode, memnull));
    context->sealedStatus = true;
    checkRet(Coil_AddAllNecessaryHeaders(context));
    return true;
}

// TODO: the result type needs to convey error vs empty content
Mem Coil_GetContent(RouteContext *context) {
    bool hasContentLength = map_has(context->headers, mkString("content-length"));
    bool hasTransferEncoding = map_has(context->headers, mkString("transfer-encoding"));
    // if(hasContentLength && hasTransferEncoding) return memnull; // unreachable, caught in threadRoutine
    if(!hasContentLength && !hasTransferEncoding) return memnull;
    if(hasTransferEncoding) hasContentLength = false;

    if(hasContentLength) {
        HttpH_ContentLength contentLength = memExtract(HttpH_ContentLength, map_get(context->headers, mkString("content-length")));
        if(contentLength.length > CONTENT_LIMIT) return memnull;
        Mem mem = AllocateBytes(contentLength.length);
        ResultRead r = stream_read(context->s, mem);
        if(r.error || r.partial) return memnull;
        return mem;
    }
    else if(hasTransferEncoding) {
        HttpH_TransferEncoding transferEncoding = memExtract(HttpH_TransferEncoding, map_get(context->headers, mkString("transfer-encoding")));

        Mem mem = memnull;
        while(transferEncoding.codings.len != 0) {
            HttpTransferCoding coding = dynar_peek(HttpTransferCoding, &transferEncoding.codings);
            dynar_pop(HttpTransferCoding, &transferEncoding.codings);

            // printf("CODING %.*s\n", (int)coding.coding.len, coding.coding.s);

            // printf("RBUFFER\n");
            write(STDOUT_FILENO, context->s->rbuffer.s, context->s->rbuffer.len);

            if(mem_eq(coding.coding, mkString("chunked"))) {
                StringBuilder sb = mkStringBuilder();
                Stream s = mkStreamSb(&sb);

                while(true) {
                    u64 chunkLength;
                    checkRetVal(parseU64FromHex(context->s, &chunkLength, false), memnull);
                    HttpChunkExtensions ext = Http_parseChunkExtensions(context->s, ALLOC);
                    if(isNone(ext)) return memnull;
                    checkRetVal(Http_parseCRLF(context->s), memnull);

                    if(chunkLength > CONTENT_LIMIT) return memnull;
                    if(sb.len + chunkLength > CONTENT_LIMIT) return memnull;

                    if(chunkLength == 0) { break; } // final chunk
                    checkRetVal(stream_dumpInto(context->s, &s, chunkLength, false), memnull);
                    checkRetVal(Http_parseCRLF(context->s), memnull);
                }

                MaybeChar c = stream_peekChar(context->s);
                if(isNone(c)) return memnull;

                if(isJust(c) && c.value == HTTP_CR) {
                    checkRetVal(Http_parseCRLF(context->s), memnull);
                }
                else {
                    // i fucking hate trailer fields
                    while(true) {
                        HttpError result = Http_parseHeaderField(context->s, context->headers);
                        bool crlf = Http_parseCRLF(context->s);
                        if(!crlf && result == HTTPERR_SUCCESS) { result = HTTPERR_INVALID_HEADER_FIELD; }

                        if(result != HTTPERR_SUCCESS) {
                            context->error = result;
                            context->statusCode = 400;
                            Handle(context, context->lastRouter->handler_badRequest);
                            return memnull;
                        }

                        bool finalCrlf = Http_parseCRLF(&s);
                        if(finalCrlf) break;
                    }
                }

                mem = sb_build(sb);
            }
            // NOTE: I wanted to test this via curl, but for some reason "Transfer-Encoding: gzip, chunked" couldn't be properly sent

            // else if(mem_eq(coding.coding, mkString("gzip"))) {
            //     Mem un = Gzip_decompress(mem, ALLOC);
            //     if(isNull(un)) return memnull;
            //     mem = un;
            // }
            else {
                // unreachable
                return memnull;
            }
        }

        return mem;
    }
    return memnull;
}

typedef struct {
    usz id;

    struct sockaddr_in addr;
    int clientSock;

    Router *router;
} Connection;

void *threadRoutine(void *_connection) {
    Connection connection = *(Connection *)_connection;
    Free(_connection);

    Log_format1(LOG_INFO, "<%d> Started thread routine", connection.id);

    // https://stackoverflow.com/questions/2876024/linux-is-there-a-read-or-recv-from-socket-with-timeout
    struct timeval timeout = { .tv_sec = 60 }; // for some bizarre reason this works only half the time
    setsockopt(connection.clientSock, SOL_SOCKET, SO_RCVTIMEO, (void *)&timeout, sizeof(struct timeval));

    Stream s = mkStreamFd(connection.clientSock);
    stream_wbufferEnable(&s, 4096);
    stream_rbufferEnable(&s, 4096);

    bool connectionPersists = false;

    // NOTE: to invert the ALLOC_POP usage, so that it can be put in cleanup
    ALLOC_PUSH_DUMMY();

    do {
        // Timeout handling
        MaybeChar any = stream_peekChar(&s);
        if(isNone(any)) {
            goto cleanup;
        }

        ALLOC_POP(); // pop dummy
        ALLOC_PUSH(mkAlloc_LinearExpandableA(ALLOC_GLOBAL));

        RouteContext context = ((RouteContext){
            .s = &s,
            .mainRouter = connection.router,
            .lastRouter = connection.router,
        });

        Http11RequestLine requestLine = Http_parseHttp11RequestLine(&s, ALLOC);
        stream_rlimitDisable(&s);

        if(isNone(requestLine)) {
            context.error = requestLine.errmsg;
            Log_format2(LOG_ERROR, "<%d> Couldn't parse request line. errmsg = %d", connection.id, requestLine.errmsg);

            if(isFail(requestLine, HTTPERR_INTERNAL_ERROR)) {
                context.statusCode = 500;
                Handle(&context, connection.router->handler_internalError);
            }
            else if(isFail(requestLine, HTTPERR_UNKNOWN_METHOD)) {
                context.statusCode = 501;
                Handle(&context, connection.router->handler_notImplemented);
            }
            else if(isFail(requestLine, HTTPERR_REQUEST_TARGET_TOO_LONG)) {
                // NOTE: does this need a separate callback?
                context.statusCode = 414;
                Handle(&context, connection.router->handler_badRequest);
            }
            else {
                context.statusCode = 400;
                Handle(&context, connection.router->handler_badRequest);
            }

            goto cleanup;
        }

        if(requestLine.target.path.segments.len != 0) {
            if(dynar_peek(String, &requestLine.target.path.segments).len == 0) requestLine.target.path.segments.len -= 1;
        }

        if(Log_is1) {
            StringBuilder sb = mkStringBuilder();
            dynar_foreach(String, &requestLine.target.path.segments) {
                sb_appendChar(&sb, '/');
                sb_appendMem(&sb, loop.it);
            }
            if(sb.len == 0) {
                sb_appendChar(&sb, '/');
            }

            String path = sb_build(sb);
            Log_format2(LOG_INFO, "<%d> Request at \"%.*s\"", connection.id, path.len, path.s);
        }

        Map headers = mkMap();

        // TODO: there should probably be a check that we're being trolled by an infinite stream of headers
        while(true) {
            HttpError result = Http_parseHeaderField(&s, &headers);
            bool crlf = Http_parseCRLF(&s);
            if(!crlf && result == HTTPERR_SUCCESS) { result = HTTPERR_INVALID_HEADER_FIELD; }

            if(result != HTTPERR_SUCCESS) {
                context.error = result;

                if(result == HTTPERR_INTERNAL_ERROR) {
                    context.statusCode = 500;
                    Handle(&context, connection.router->handler_internalError);
                }
                else {
                    context.statusCode = 400;
                    Handle(&context, connection.router->handler_badRequest);
                }

                goto cleanup;
            }

            bool finalCrlf = Http_parseCRLF(&s);
            if(finalCrlf) break;
        }

        if(!map_has(&headers, mkString("host"))) {
            context.statusCode = 400;
            context.error = HTTPERR_BAD_HOST;
            Handle(&context, connection.router->handler_badRequest);
            goto cleanup;
        }

        HttpH_Connection *connectionHeader = memExtractPtr(HttpH_Connection, map_get(&headers, mkString("connection")));
        bool containsClose = connectionHeader != null
            ? dynar_containsString(&connectionHeader->connectionOptions, mkString("close")) : false;
        bool containsKeepalive = connectionHeader != null
            ? dynar_containsString(&connectionHeader->connectionOptions, mkString("keep-alive")) : false;

        // if(map_has(&headers, mkString("content-length")) && map_has(&headers, mkString("transfer-encoding"))) {
        //     context.statusCode = 400;
        //     context.error = HTTPERR_BAD_CONTENT_LENGTH;
        //     Handle(&context, connection.router->handler_badRequest);
        //     goto cleanup;
        // }

        if(map_has(&headers, mkString("transfer-encoding"))) {
            HttpH_TransferEncoding transferEncoding = memExtract(HttpH_TransferEncoding, map_get(&headers, mkString("transfer-encoding")));
            dynar_foreach(HttpTransferCoding, &transferEncoding.codings) {
                if(loop.index == transferEncoding.codings.len - 1 && !mem_eq(loop.it.coding, mkString("chunked"))) {
                    // printf("BAD A\n");
                    context.statusCode = 400;
                    context.error = HTTPERR_BAD_TRANSFER_CODING;
                    Handle(&context, connection.router->handler_badRequest);
                    goto cleanup;
                }

                if(loop.index != transferEncoding.codings.len - 1 && mem_eq(loop.it.coding, mkString("chunked"))) {
                    // printf("BAD B\n");
                    context.statusCode = 400;
                    context.error = HTTPERR_BAD_TRANSFER_CODING;
                    Handle(&context, connection.router->handler_badRequest);
                    goto cleanup;
                }

                if(
                !mem_eq(loop.it.coding, mkString("chunked")) &&
                // !mem_eq(loop.it.coding, mkString("gzip")) &&
                true) {
                    context.statusCode = 501;
                    context.error = HTTPERR_UNKNOWN_TRANSFER_CODING;
                    Handle(&context, connection.router->handler_notImplemented);
                    goto cleanup;
                }
            }
        }

        if(containsClose)
            { connectionPersists = false; }
        // NOTE: this seems to be as per RFC-9112, but Firefox automatically starts
        // a new connection even though I send version 1.1

        // NOTE: huh, now it doesnt?? did i test it wrong or what the hell is happening
        else if(requestLine.version.value >= Http_getVersion(1, 1) || containsKeepalive)
            { connectionPersists = true; }
        else
            { connectionPersists = false; }

        // TODO: we have the headers, including the Host, now
        // we reconstruct the target URI and parse it

        // TODO: with the target URI reconstructed, we can now,
        // *gulp*, convert the path+query back into a string to
        // feed to the router
        // Should I extend the Uri/UriPath structs to include a
        // string representation? I probably should

        Map routeMatches = mkMap();
        context = ((RouteContext){
            .s = &s,
            .clientVersion = requestLine.version,
            .method = requestLine.method,
            .headers = &headers,

            .originalPath = requestLine.target.path,
            .relatedPath = requestLine.target.path,

            .mainRouter = connection.router,
            .lastRouter = connection.router,

            .persist = true,

            .query = requestLine.target.query,

            .matches = &routeMatches,
        });

        // MapIter iter = map_iter(&headers);
        // while(!map_iter_end(&iter)) {
        //     MapEntry entry = map_iter_next(&iter);
        //     HttpH_Unknown header = memExtract(HttpH_Unknown, entry.val);
        //     String value = header.value;
        //     value = value;
        //
        //     // printf("HEADER NAME: %.*s\n", (int)entry.key.len, entry.key.s);
        //     // printf("HEADER VALUE: %.*s\n", (int)value.len, value.s);
        //     // printf("-----------\n");
        // }

        Route route = getRoute(connection.router, &context);
        if(isNone(route)) {
            if(isFail(route, ROUTE_ERR_FOUND_URI)) {
                Log_format2(LOG_ERROR, "<%d> Couldn't find an appropriate route with this method", connection.id);

                context.statusCode = 405;
                context.allowedMethodMask = route.methodMask;
                checkDo(Handle(&context, connection.router->handler_badRequest), goto cleanup);
                tryDo(stream_writeFlush(&s), goto cleanup);
                continue;
            }
            else {
                Log_format2(LOG_ERROR, "<%d> Couldn't find an appropriate route", connection.id);

                context.statusCode = 404;
                checkDo(Handle(&context, connection.router->handler_routeNotFound), goto cleanup);
                tryDo(stream_writeFlush(&s), goto cleanup);
                continue;
            }
        }

        Log_format2(LOG_INFO, "<%d> Trying to handle the route", connection.id);
        if(!Handle(&context, route.handler)) {
            Log_format1(LOG_ERROR, "<%d> Couldn't handle the route", connection.id);

            context.statusCode = 500;
            Handle(&context, connection.router->handler_internalError);
            goto cleanup;
        }
        Log_format2(LOG_INFO, "<%d> Route handled successfully", connection.id);

        if(!context.persist) connectionPersists = false;

        if(isNone(stream_writeFlush(&s))) {
            Log_format1(LOG_ERROR, "<%d> Couldn't flush the buffer", connection.id);
            goto cleanup;
        }

    } while(connectionPersists);

cleanup:
    Log_format1(LOG_INFO, "<%d> Closing connection", connection.id);
    stream_writeFlush(&s);
    ALLOC_POP();
    Free(s.wbuffer.s);
    Free(s.rbuffer.s);
    close(connection.clientSock);

    return null;
}

#define CoilCallback(name, body) \
bool name(RouteContext *context, Mem arg) { context = context; arg = arg; { body; } }

#define CoilCallbackStr(name, arg, body) \
bool name(RouteContext *context, String arg) { context = context; arg = arg; { body; } }

#define CoilCallbackArg(name, argty, argname, body) \
bool name(RouteContext *context, Mem arg) { context = context; arg = arg; argty *argname = memExtractPtr(argty, arg); { body; } }

CoilCallbackArg(CoilCB_fileTree, FileTreeRouter, fileTree, {
    File file = getFileTree(fileTree, context->relatedPath);
    if(isNone(file)) {
        return Coil_NotFound(context);
    }

    checkRet(Coil_StatusLine(context, 200));
    checkRet(Coil_AddFile(context, file));
    // Stream fileStream = mkStreamStr(file.data);
    // Dynar(HttpTransferCoding) codings = mkDynar(HttpTransferCoding);
    // cont(result) Coil_AddContentStream(context, &fileStream, &codings);

    return true;
})

CoilCallbackStr(CoilCB_file, filePath, {
    File file = getFile(filePath, ALLOC);
    if(isNone(file)) {
        return Coil_NotFound(context);
    }

    checkRet(Coil_StatusLine(context, 200));
    checkRet(Coil_AddFile(context, file));

    return true;
})

CoilCallbackStr(CoilCB_data, data, {
    checkRet(Coil_StatusLine(context, 200));
    checkRet(Coil_AddContent(context, data));
    return true;
})

CoilCallback(CoilCB_error, {
    HttpStatusCode statusCode = context->statusCode;
    checkRet(Coil_StatusLine(context, statusCode));
    String content = mkString("<body><h1>Something very bad has happened</h1></body>");

    switch(statusCode) {
        case 400:
            // printf("ERROR CODE %d\n", context->error);
            content = mkString("<html><body><h1>400 Bad Request</h1><h2>Your request is very bad (uncool and bad and not cool!) >:(</h2></body></html>");
            break;
        case 404:
            content = mkString("<html><body><h1>404 Not Found</h1><h2>Sorry we don't have this here</h2></body></html>");
            break;
        case 405:
            tryRet(stream_write(context->s, mkString("Allow: ")));
            bool written = false;
            for(int i = 0; context->allowedMethodMask; i++) {
                if(i != 0 && context->allowedMethodMask & 1) {
                    MaybeString s = Http_getMethod(i);
                    if(isJust(s)) {
                        if(written) {
                            checkRet(stream_writeChar(context->s, ','));
                            checkRet(stream_writeChar(context->s, ' '));
                        }
                        tryRet(stream_write(context->s, s.value));
                        written = true;
                    }
                }
                context->allowedMethodMask >>= 1;
            }
            checkRet(Http_writeCRLF(context->s));
            content = mkString("<html><body><h1>405 Method Not Allowed</h1><h2>that is a very nuh uh method for this so called resource</h2></body></html>");
            break;
        case 414:
            content = mkString("<html><body><h1>414 URI Too Long</h1><h2>your URI is too long and girthy</h2></body></html>");
            break;
        case 500:
            content = mkString("<html><body><h1>500 Internal Server Error</h1><h2>The server has commitet ded (shouldn't have written it in C)</h2></body></html>");
            break;
        case 501:
            content = mkString("<html><body><h1>501 Not Implemented</h1><h2>This very cool feature is not implemented here :(</h2></body></html>");
            break;
    }

    checkRet(Coil_AddContent(context, content));
    return true;
})

typedef struct {
    bool error;
    int value;
} MaybeSocket;

MaybeSocket Coil_GetSocket(u16 port, int backlog) {
    int result;

    int sock = result = socket(AF_INET, SOCK_STREAM, 0);

    if(result == -1) {
        Log_format0(LOG_ERROR, "Couldn't create a socket. errno = %d", errno);
        return none(MaybeSocket);
    }

    Log_format2(LOG_INFO, "Created a socket. fd = %d", sock);

    struct sockaddr_in addr = (struct sockaddr_in){
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr = (struct in_addr){
            .s_addr = htonl(INADDR_ANY)
        },
    };

    Log_format2(LOG_INFO, "Binding the socket to port %d", port);
    result = bind(sock, (struct sockaddr *)&addr, sizeof(struct sockaddr_in));

    if(result == -1) {
        Log_format0(LOG_ERROR, "Couldn't bind the socket. errno = %d", errno);
        return none(MaybeSocket);
    }

    Log_message2(LOG_INFO, "Bound the socket successfully");

    result = listen(sock, backlog);

    if(result == -1) {
        Log_format0(LOG_ERROR, "Couldn't make the socket listen. errno = %d", errno);
        return none(MaybeSocket);
    }

    Log_message2(LOG_INFO, "Socket is listening to connections");

    return just(MaybeSocket, sock);
}

Router mkRouter() {
    Router router = (Router){
        .alloc = ALLOC,
        .routes = mkDynar(Route),

        .handler_routeNotFound  = mkHandler(CoilCB_error),
        .handler_internalError  = mkHandler(CoilCB_error),
        .handler_badRequest     = mkHandler(CoilCB_error),
        .handler_notImplemented = mkHandler(CoilCB_error),
    };
    return router;
}

bool Coil_Run(int sock, Router *router) {
    // NOTE: if client dies, we likely encounter an error on
    // our next read() in threadRoutine. We then goto cleanup and
    // try to flush the stream via write(), causing SIGPIPE
    struct sigaction sigHandlerNone = {
        .sa_handler = SIG_IGN,
    };
    struct sigaction sigHandlerOld;
    int result = sigaction(SIGPIPE, &sigHandlerNone, &sigHandlerOld);
    if(result != 0) {
        Log_format0(LOG_WARNING, "Couldn't replace SIGPIPE handler. errno = %d", errno);
        // NOTE: does this really warrant quitting?
        // return false;
    }

    Log_message0(LOG_INFO, "Request loop started");

    usz connectionId = 1000;

    while(true) {
        struct sockaddr_in caddr = {0};
        socklen_t caddrLen = sizeof(struct sockaddr_in);
        // TODO: figure out if this works for IPv6
        int csock = accept(sock, (struct sockaddr *)&caddr, &caddrLen);

        int result;

        Connection _connection = {
            .addr = caddr,
            .clientSock = csock,
            .router = router,
            .id = connectionId,
        };
        connectionId += 1;

        if(Log_is2) {
            u32 ip = caddr.sin_addr.s_addr;
            u16 port = caddr.sin_port;
            u8 a = ((ip & 0x000000ff) >> 0);
            u8 b = ((ip & 0x0000ff00) >> 8);
            u8 c = ((ip & 0x00ff0000) >> 16);
            u8 d = ((ip & 0xff000000) >> 24);

            Log_format2(LOG_INFO, "<%d> Accepted client. IP: %d.%d.%d.%d:%d", _connection.id, a, b, c, d, port);
        }
        else {
            Log_format0(LOG_INFO, "<%d> Accepted client", _connection.id);
        }

        AllocateVarC(Connection, connection, _connection, ALLOC_GLOBAL);

        pthread_t thread;
        pthread_attr_t threadAttr;
        result = pthread_attr_init(&threadAttr);
        result = pthread_attr_setdetachstate(&threadAttr, PTHREAD_CREATE_DETACHED);
        result = pthread_create(&thread, &threadAttr, threadRoutine, connection);
        result = pthread_attr_destroy(&threadAttr);
        if(result) {}
    }

    result = sigaction(SIGPIPE, &sigHandlerOld, null);
    if(result != 0) return false;

    int closeResult = close(sock);
    // printf("CLOSE %d\n", closeResult);

    return true;
}

#endif // __LIB_COIL
