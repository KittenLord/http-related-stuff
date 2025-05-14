#include <stdio.h>
#include <stdlib.h>

#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>

#include <pthread.h>

#include <stream.h>
#include <compression/gzip.c>
#include <compression/zlib.c>

#include <crypto/sha.c>
#include "http.c"
#include <map.h>
#include <hashmap.h>
#include <text.h>

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

#ifndef CONTENT_LIMIT
#define CONTENT_LIMIT 100000000
#endif

#define GET_NO_HEAD     (1 << HTTP_GET)
#define HEAD            (1 << HTTP_HEAD)
#define GET             (GET_NO_HEAD | HEAD)
#define POST            (1 << HTTP_POST)
#define PUT             (1 << HTTP_PUT)
#define DELETE          (1 << HTTP_DELETE)
#define CONNECT         (1 << HTTP_CONNECT)
#define OPTIONS         (1 << HTTP_OPTIONS)
#define TRACE           (1 << HTTP_TRACE)
#define ALL             u64max
typedef u64 HttpMethodMask;

typedef struct RouteContext RouteContext;
typedef bool (RouteCallback)(RouteContext *, Mem);

typedef struct {
    bool isMatch; // match a single segment
    bool isWildcard; // match all (or none) remaining segments
    String value;
} RoutePathSegment;

typedef struct {
    bool error;
    Dynar(RoutePathSegment) segments;
} RoutePath;

typedef struct {
    RouteCallback *callback;
    Mem argument;
} RouteHandler;
#define mkHandlerArg(c, a) ((RouteHandler){ .callback = (c), .argument = (a) })
#define mkHandler(a) mkHandlerArg((a), memnull)
#define Handle(c, h) ((h).callback((c), (h).argument))

typedef enum {
    ROUTE_ERR_NONE,

    ROUTE_ERR_FOUND_URI,
} RouteError;

typedef struct {
    bool error;
    RouteError errmsg;

    HttpMethodMask methodMask;

    String subdomain;
    RoutePath path;

    RouteHandler handler;
} Route;

typedef struct {
    Alloc *alloc;

    Dynar(Route) routes;

    RouteHandler handler_routeNotFound;
    RouteHandler handler_internalError;
    RouteHandler handler_badRequest;
    RouteHandler handler_notImplemented;
} Router;

struct RouteContext {
    Stream *s;
    Router *mainRouter;
    Router *lastRouter;

    // Present if error
    HttpError error;
    HttpStatusCode statusCode;
    HttpMethodMask allowedMethodMask;

    // Present if success
    HttpVersion clientVersion;
    HttpMethod method;
    Map *headers;
    UriPath originalPath;
    UriPath relatedPath;
    bool persist;
    bool sealedStatus;
    bool sealedHeaders;
    bool sealedContent;
};

typedef struct {
    bool error;

    Mem data;
    HttpMediaType mediaType;
    time_t modTime;

    Hash256 hash;
    bool hasHash;

    Mem gzip;
    Mem zlib;
} File;

typedef struct {
    Alloc *alloc;

    bool disableCaching;

    bool doHash;
    bool doGzip;
    bool doZlib;

    HASHMAP(String, File) hm;
} FileStorage;

typedef struct {
    Alloc *alloc;
    FileStorage *storage;
    UriPath basePath;
} FileTreeRouter;

typedef struct {
    struct sockaddr_in addr;
    int clientSock;

    Router *router;
} Connection;

HttpMediaType getMediaType(String extension) {
    if(isNull(extension)) return mkHttpMediaType("application", "octet-stream");

#define Ext(s) mem_eq(extension, mkString(s)) ||
#define MType(s, b, c) if(s false) { return mkHttpMediaType((b), (c)); }
    MType(Ext("jpeg") Ext("jpg"), "image", "jpeg")
    MType(Ext("png"), "image", "png")
    MType(Ext("html") Ext("htm"), "text", "html")
    MType(Ext("css"), "text", "css")
    MType(Ext("json"), "application", "json")
    MType(Ext("pdf"), "application", "pdf")

    // NOTE: as per RFC-2046, text/plain MUST have CRLF as newlines,
    // but I'm going to ignore that
    MType(Ext("txt"), "text", "plain")
#undef MType
#undef Ext

    return mkHttpMediaType("application", "octet-stream");
}

time_t getFileModTime(String path) {
    struct stat s = {0};
    int result = stat(fixchar path.s, &s);

    if(result != 0) return 0;
    return s.st_mtime;
}

String getFileExtension(String path) {
    usz i = 0;
    bool foundPeriod = false;
    for(i = path.len - 1; true /*|| i >= 0*/; i--) {
        if(i == path.len - 1 && path.s[i] == '.') break;
        if(path.s[i] == '/') break;
        if(path.s[i] == '.') {
            foundPeriod = true;
            break;
        }
        if(i == 0) break;
    }

    if(!foundPeriod) return memnull;
    return memIndex(path, i + 1);
}

// TODO: we probably need a getFileStream(), but I'm not sure how to
// handle close() of the fd

File getFile(String path, Alloc *alloc) {
    printf("GET FILE\n");
    struct stat s = {0};
    int result = stat(fixchar path.s, &s);
    if(result != 0) return none(File);

    FILE *file = fopen(fixchar path.s, "r");
    if(file == null) return none(File);
    int fd = fileno(file);
    if(fd == -1) return none(File);

    Mem data = AllocateBytesC(alloc, s.st_size);
    isz bytesRead = read(fd, data.s, data.len);
    if(bytesRead < 0 || (usz)bytesRead != data.len) {
        FreeC(alloc, data.s);
        return none(File);
    }

    String extension = getFileExtension(path);
    HttpMediaType mediaType = getMediaType(extension);

    return (File){
        .data = data,
        .mediaType = mediaType,
        .modTime = s.st_mtime,
    };
}

// TODO: error checking
void storageFillFile(File *file, FileStorage *storage) {
    if(storage->doHash) {
        file->hash = Sha256(file->data);
        file->hasHash = true;
    }

    if(storage->doGzip) {
        file->gzip = Gzip_compress(file->data, storage->alloc);
    }

    if(storage->doZlib) {
        file->zlib = Zlib_compress(file->data, storage->alloc);
    }
}

File getFileStorage(String path, FileStorage *storage) {
    if(storage == null) {
        return getFile(path, ALLOC);
    }

    if(storage->disableCaching) {
        File file = getFile(path, ALLOC);
        if(isNone(file)) return none(File);
        storageFillFile(&file, storage);
        return file;
    }

    // NOTE: if the file has been deleted, we don't look for it (maybe change?)
    time_t modTime = getFileModTime(path);
    if(modTime == 0) return none(File);

    File file;
    Map *map = hm_getMap(&storage->hm, path);

    // TODO: this does not copy memory from hashmap to user, thus the
    // hashmap can't free old versions of files - which is fine, if the
    // files modify rarely (or at all), but I'm not sure if this is good
    map_block(map) {
        File *supposedFile = (File *)map_get(map, path).s;
        if(supposedFile != null && supposedFile->modTime == modTime) {
            file = *supposedFile;
            continue;
        }

        file = getFile(path, storage->alloc);
        if(isNone(file)) return none(File);
        storageFillFile(&file, storage);

        map_set(map, path, memPointer(File, &file));
    }

    return file;
}

File getFileTree(FileTreeRouter *ftrouter, UriPath subPath) {
    UriPath result = Uri_pathMoveRelatively(ftrouter->basePath, subPath, ALLOC);
    if(!Uri_pathHasPrefix(ftrouter->basePath, result)) return none(File);

    StringBuilder sb = mkStringBuilder();
    dynar_foreach(String, &result.segments) {
        sb_appendMem(&sb, loop.it);
        if(loop.index != result.segments.len - 1) {
            sb_appendChar(&sb, '/');
        }
    }

    String filePath = sb_build(sb);
    return getFileStorage(filePath, ftrouter->storage);
}

MaybeString parseRoutePathMatch(Stream *s, Alloc *alloc) {
    stream_popChar(s);
    StringBuilder sb = mkStringBuilder();
    sb.alloc = alloc;
    MaybeChar c;

    while(isJust(c = stream_peekChar(s)) && c.value != '}') {
        if(c.value == '/') { return none(MaybeString); }
        sb_appendChar(&sb, c.value);
        stream_popChar(s);
    }

    if(isNone(c)) return none(MaybeString);
    stream_popChar(s);
    c = stream_peekChar(s);
    if(isJust(c) && c.value != '/') return none(MaybeString);
    return just(MaybeString, sb_build(sb));
}

RoutePath parseRoutePath(Stream *s, Alloc *alloc) {
    RoutePath result = {0};
    result.segments = mkDynarCA(RoutePathSegment, 8, alloc);

    MaybeChar c;
    while(isJust(c = stream_peekChar(s))) {
        if(c.value != '/') return none(RoutePath);
        stream_popChar(s);

        c = stream_peekChar(s);
        if(isNone(c)) return result; // NOTE: trailing empty segment not counted

        // TODO: allow for escaped '{' and '}'

        RoutePathSegment segment = {0};

        if(c.value == '{') {
            MaybeString matchSegment = parseRoutePathMatch(s, alloc);
            if(isNone(matchSegment)) return none(RoutePath);

            segment.value = matchSegment.value;
            segment.isMatch = true;
        }
        else {
            StringBuilder sb = mkStringBuilder();
            sb.alloc = alloc;

            // TODO: Most of these allocations are completely unnecessary,
            // as long as the lifetime of the string behind the stream is
            // the same as the supposed return value of this... I need to
            // spend some time thinking about this in general, it's not
            // like we benefit from the stream being opaque here...

            while(isJust(c = stream_peekChar(s)) && c.value != '/') {
                sb_appendChar(&sb, c.value);
                stream_popChar(s);
            }

            segment.value = sb_build(sb);
        }

        dynar_append(&result.segments, RoutePathSegment, segment, _);
    }

    if(mem_eq(dynar_peek(RoutePathSegment, &result.segments).value, mkString("*"))) {
        dynar_peek(RoutePathSegment, &result.segments).isWildcard = true;
    }

    return result;
}

FileTreeRouter mkFileTreeRouter(String spath, FileStorage *storage) {
    Alloc *alloc = ALLOC;
    Stream s = mkStreamStr(spath);
    UriPath path = Uri_parsePathRootless(&s, alloc);
    // if(isJust(stream_peekChar(&s))) {
    //     // NOTE: error
    // }

    FileTreeRouter ftrouter = {
        .alloc = alloc,
        .basePath = path,
        .storage = storage,
    };

    return ftrouter;
}

bool routePathMatches(RoutePath routePath, UriPath uriPath) {
    usz i = 0;
    for(i = 0; i < routePath.segments.len && i < uriPath.segments.len; i++) {
        RoutePathSegment rs = dynar_index(RoutePathSegment, &routePath.segments, i);
        String us = dynar_index(String, &uriPath.segments, i);

        if(rs.isWildcard) return true;
        if(rs.isMatch) continue;

        if(!mem_eq(rs.value, us)) {
            return false;
        }
    }

    if(routePath.segments.len == uriPath.segments.len) return true;
    if(i < routePath.segments.len && dynar_index(RoutePathSegment, &routePath.segments, i).isWildcard) return true;
    return false;
}

bool routeMethodMatches(Route route, HttpMethod method) {
    return ((route.methodMask) & (1 << method)) != 0;
}

Route getRoute(Router *r, RouteContext *context) {
    Route route = none(Route);
    dynar_foreach(Route, &r->routes) {
        if(!routePathMatches(loop.it.path, context->relatedPath)) continue;

        route.errmsg = ROUTE_ERR_FOUND_URI;
        route.methodMask |= loop.it.methodMask;

        if(!routeMethodMatches(loop.it, context->method)) continue;
        route = loop.it;
        break;
    }

    if(isNone(route)) return route;

    // TODO: we've found the route, fill the context
    // with all pattern matched path segments, chop
    // off the matched part of the path (for routes
    // that end with /*), etc

    RoutePath routePath = route.path;
    UriPath relatedPath = context->relatedPath;

    dynar_foreach(RoutePathSegment, &routePath.segments) {
        if(loop.it.isWildcard) break;
        dynar_remove(String, &relatedPath.segments, 0);
    }

    context->relatedPath = relatedPath;

    return route;
}

void addRoute(Router *r, HttpMethodMask methodMask, String host, String path, RouteHandler handler) {
    Stream s = mkStreamStr(path);
    RoutePath routePath = parseRoutePath(&s, r->alloc);

    // TODO: signal error
    if(isNone(routePath)) return;

    Route route = {
        .methodMask = methodMask,
        .subdomain = host,
        .path = routePath,
        .handler = handler,
    };

    dynar_append(&r->routes, Route, route, _);
    return;
}

bool Placeholder_AddDate(RouteContext *context) {
    pure(result) flattenStreamResultWrite(stream_write(context->s, mkString("Date: ")));
    cont(result) Http_writeDateNow(context->s);
    cont(result) Http_writeCRLF(context->s);
    return result;
}

bool Placeholder_AddETag(RouteContext *context, Mem data, bool isWeak) {
    pure(result) flattenStreamResultWrite(stream_write(context->s, mkString("ETag: ")));
    if(isWeak) {
        cont(result) stream_writeChar(context->s, 'W');
        cont(result) stream_writeChar(context->s, '/');
    }

    cont(result) stream_writeChar(context->s, '\"');
    Stream s = mkStreamStr(data);
    cont(result) writeBytesToBase64(&s, context->s, false, false);
    cont(result) stream_writeChar(context->s, '\"');
    cont(result) Http_writeCRLF(context->s);
    return result;
}

bool Placeholder_AddLastModified(RouteContext *context, time_t lastModified) {
    pure(result) flattenStreamResultWrite(stream_write(context->s, mkString("Last-Modified: ")));
    cont(result) Http_writeDate(context->s, lastModified);
    cont(result) Http_writeCRLF(context->s);
    return result;
}

bool Placeholder_AddHeader(RouteContext *context, String header, String value) {
    pure(result) flattenStreamResultWrite(stream_write(context->s, header));
    cont(result) stream_writeChar(context->s, ':');
    cont(result) stream_writeChar(context->s, ' ');
    cont(result) flattenStreamResultWrite(stream_write(context->s, value));
    cont(result) Http_writeCRLF(context->s);
    return result;
}

bool Placeholder_SealHeaders(RouteContext *context) {
    if(!context->sealedStatus) return false;
    if(context->sealedHeaders) return false;

    pure(result) Http_writeCRLF(context->s);
    if(!result) return false;

    context->sealedHeaders = true;
    return true;
}

bool Placeholder_AddContentLength(RouteContext *context, u64 length) {
    pure(result) flattenStreamResultWrite(stream_write(context->s, mkString("Content-Length: ")));
    cont(result) writeU64ToDecimal(context->s, length);
    cont(result) Http_writeCRLF(context->s);
    return result;
}

bool Placeholder_AddAllNecessaryHeaders(RouteContext *context) {
    pure(result) Placeholder_AddDate(context);
    cont(result) Placeholder_AddHeader(context, mkString("Connection"), mkString("keep-alive"));
    return result;
}

bool Placeholder_AddContent(RouteContext *context, Mem content) {
    pure(result) Placeholder_AddContentLength(context, content.len);
    cont(result) Placeholder_SealHeaders(context);
    if(context->method != HEAD) {
        cont(result) flattenStreamResultWrite(stream_write(context->s, content));
    }
    return result;
}

bool Placeholder_AddContentType(RouteContext *context, HttpMediaType mediaType) {
    pure(result) flattenStreamResultWrite(stream_write(context->s, mkString("Content-Type: ")));
    cont(result) Http_writeMediaType(context->s, mediaType);
    cont(result) Http_writeCRLF(context->s);
    return result;
}

bool Placeholder_AddFile(RouteContext *context, File file) {
    pure(result) Placeholder_AddContentType(context, file.mediaType);
    if(file.hasHash) {
        cont(result) Placeholder_AddETag(context, mkMem(file.hash.data, 256 / 8), false);
    }
    cont(result) Placeholder_AddLastModified(context, file.modTime);
    cont(result) Placeholder_AddContent(context, file.data);
    return result;
}

bool Placeholder_AddTransferEncoding(RouteContext *context, Dynar(HttpTransferCoding) *codings) {
    pure(result) flattenStreamResultWrite(stream_write(context->s, mkString("Transfer-Encoding: ")));
    dynar_foreach(HttpTransferCoding, codings) {
        if(loop.index != 0) {
            cont(result) stream_writeChar(context->s, ',');
            cont(result) stream_writeChar(context->s, ' ');
        }
        cont(result) flattenStreamResultWrite(stream_write(context->s, loop.it.coding));
    }
    cont(result) Http_writeCRLF(context->s);
    return result;
}

// NOTE: this would've been much cooler if we had lazy streams
// (i.e. we could make a nested stream that applies all
// codings, and then lazily request 1024 at a time from it)

// NOTE: also Firefox doesn't support any TE except for chunked,
// so I won't be able to test this lol
bool Placeholder_AddContentStream(RouteContext *context, Stream *s, Dynar(HttpTransferCoding) *codings) {
    if(context->clientVersion.value < Http_getVersion(1, 1)) {
        pure(result) Placeholder_SealHeaders(context);
        if(context->method != HEAD) {
            cont(result) stream_dumpInto(s, context->s, 0, true);
            context->persist = false;
        }
        return result;
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

    pure(result) true;
    dynar_append(codings, HttpTransferCoding, mkHttpTransferCoding("chunked"), result);
    cont(result) Placeholder_AddTransferEncoding(context, codings);
    cont(result) Placeholder_SealHeaders(context);

    if(context->method == HEAD) return result;

    dynar_foreach(HttpTransferCoding, codings) {
        if(mem_eq(loop.it.coding, mkString("chunked"))) {
            ResultRead r;

            byte buffer[1024];

            while(isJust(r = stream_read(s, mkMem(buffer, 1024)))) {
                u64 len = r.read;
                if(len == 0) break;

                u64 elen = Sha_endian64(len);
                Stream elens = mkStreamStr(mkMem(&elen, 8));
                cont(result) writeBytesToHex(&elens, context->s, false, false);
                // here we would've put chunk extensions if we were sadists
                cont(result) Http_writeCRLF(context->s);
                cont(result) flattenStreamResultWrite(stream_write(context->s, mkMem(buffer, len)));
                cont(result) Http_writeCRLF(context->s);

                if(r.partial) break;
            }

            cont(result) stream_writeChar(context->s, '0');
            cont(result) Http_writeCRLF(context->s);

            // here we would've put trailer fields if we were sadists

            cont(result) Http_writeCRLF(context->s);
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

    return result;
}

bool Placeholder_NotFound(RouteContext *context) {
    context->statusCode = 404;
    return Handle(context, context->lastRouter->handler_routeNotFound);
}

bool Placeholder_StatusLine(RouteContext *context, HttpStatusCode statusCode) {
    pure(result) Http_writeStatusLine(context->s, 1, 1, statusCode, memnull);
    if  (result) context->sealedStatus = true;
    cont(result) Placeholder_AddAllNecessaryHeaders(context);
    return result;
}

// TODO: the result type needs to convey error vs empty content
Mem Placeholder_GetContent(RouteContext *context) {
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

            printf("CODING %.*s\n", (int)coding.coding.len, coding.coding.s);

            printf("RBUFFER\n");
            write(STDOUT_FILENO, context->s->rbuffer.s, context->s->rbuffer.len);

            if(mem_eq(coding.coding, mkString("chunked"))) {
                StringBuilder sb = mkStringBuilder();
                Stream s = mkStreamSb(&sb);

                while(true) {
                    u64 chunkLength;
                    pure(result) parseU64FromHex(context->s, &chunkLength, false);
                    HttpChunkExtensions ext = Http_parseChunkExtensions(context->s, ALLOC);
                    if(isNone(ext)) return memnull;
                    cont(result) Http_parseCRLF(context->s);
                    if(!result) return memnull;

                    if(chunkLength > CONTENT_LIMIT) return memnull;
                    if(sb.len + chunkLength > CONTENT_LIMIT) return memnull;

                    if(chunkLength == 0) { break; } // final chunk
                    cont(result) stream_dumpInto(context->s, &s, chunkLength, false);
                    cont(result) Http_parseCRLF(context->s);
                    if(!result) return memnull;
                }

                MaybeChar c = stream_peekChar(context->s);
                pure(result) isJust(c);
                if(isJust(c) && c.value == HTTP_CR) {
                    cont(result) Http_parseCRLF(context->s);
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

                if(!result) return memnull;
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

void *threadRoutine(void *_connection) {
    Connection connection = *(Connection *)_connection;
    Free(_connection);

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

        // dynar_foreach(HttpTransferCoding, &memExtract(HttpH_TE, map_get(&headers, mkString("accept-language"))).codings) {
        //     printf("Accept-Language: %.*s\n", loop.it.coding.len, loop.it.coding.s);
        //     HttpTransferCoding *current = loop.itptr;
        //     printf("    PARAM: Q\n");
        //     printf("      VALUE: %f\n", loop.it.params.q);
        //     dynar_foreach(HttpParameter, &current->params.list) {
        //         printf("    PARAM: %.*s\n", loop.it.name.len, loop.it.name.s);
        //         printf("      VALUE: %.*s\n", loop.it.value.len, loop.it.value.s);
        //     }
        // }

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
                    printf("BAD A\n");
                    context.statusCode = 400;
                    context.error = HTTPERR_BAD_TRANSFER_CODING;
                    Handle(&context, connection.router->handler_badRequest);
                    goto cleanup;
                }

                if(loop.index != transferEncoding.codings.len - 1 && mem_eq(loop.it.coding, mkString("chunked"))) {
                    printf("BAD B\n");
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
        });

        MapIter iter = map_iter(&headers);
        while(!map_iter_end(&iter)) {
            MapEntry entry = map_iter_next(&iter);
            HttpH_Unknown header = memExtract(HttpH_Unknown, entry.val);
            String value = header.value;
            value = value;

            printf("HEADER NAME: %.*s\n", (int)entry.key.len, entry.key.s);
            printf("HEADER VALUE: %.*s\n", (int)value.len, value.s);
            printf("-----------\n");
        }

        Route route = getRoute(connection.router, &context);
        if(isNone(route)) {
            if(isFail(route, ROUTE_ERR_FOUND_URI)) {
                context.statusCode = 405;
                context.allowedMethodMask = route.methodMask;
                pure(result) Handle(&context, connection.router->handler_badRequest);
                cont(result) flattenStreamResultWrite(stream_writeFlush(&s));
                if(!result) { goto cleanup; }
                continue;
            }
            else {
                context.statusCode = 404;
                pure(result) Handle(&context, connection.router->handler_routeNotFound);
                cont(result) flattenStreamResultWrite(stream_writeFlush(&s));
                if(!result) { goto cleanup; }
                continue;
            }
        }

        bool result = Handle(&context, route.handler);
        if(!result) {
            context.statusCode = 500;
            Handle(&context, connection.router->handler_internalError);
            goto cleanup;
        }

        if(!context.persist) connectionPersists = false;

        result = flattenStreamResultWrite(stream_writeFlush(&s));
        if(!result) {
            goto cleanup;
        }

    } while(connectionPersists);

cleanup:
    printf("END CONNECTION\n");
    stream_writeFlush(&s);
    ALLOC_POP();
    Free(s.wbuffer.s);
    Free(s.rbuffer.s);
    close(connection.clientSock);

    return null;
}

#define ROUTER_CALLBACK(name, body) \
bool name(RouteContext *context, Mem arg) { context = context; arg = arg; { body; } }

#define ROUTER_CALLBACK_STRING_ARG(name, arg, body) \
bool name(RouteContext *context, String arg) { context = context; arg = arg; { body; } }

#define ROUTER_CALLBACK_ARG(name, argty, argname, body) \
bool name(RouteContext *context, Mem arg) { context = context; arg = arg; argty *argname = (argty *)arg.s; { body; } }

ROUTER_CALLBACK(testCallback, {
    printf("helo\n");
    return false;
})

ROUTER_CALLBACK_ARG(fileTreeCallback, FileTreeRouter, fileTree, {
    File file = getFileTree(fileTree, context->relatedPath);
    if(isNone(file)) {
        return Placeholder_NotFound(context);
    }

    pure(result) Placeholder_StatusLine(context, 200);
    cont(result) Placeholder_AddFile(context, file);
    // Stream fileStream = mkStreamStr(file.data);
    // Dynar(HttpTransferCoding) codings = mkDynar(HttpTransferCoding);
    // cont(result) Placeholder_AddContentStream(context, &fileStream, &codings);

    return result;
})

ROUTER_CALLBACK_STRING_ARG(fileCallback, filePath, {
    File file = getFile(filePath, ALLOC);
    if(isNone(file)) {
        return Placeholder_NotFound(context);
    }

    pure(result) Placeholder_StatusLine(context, 200);
    cont(result) Placeholder_AddFile(context, file);

    return result;
})

ROUTER_CALLBACK_STRING_ARG(dataCallback, data, {
    pure(result) Placeholder_StatusLine(context, 200);
    cont(result) Placeholder_AddContent(context, data);
    return result;
})

ROUTER_CALLBACK(printCallback, {
    Mem content = Placeholder_GetContent(context);
    if(isNull(content)) {
        printf("CONTENT IS BAD\n");
    }
    else {
        printf("HERE IS YOUR CONTENT\n");
        write(STDOUT_FILENO, content.s, content.len);
    }

    pure(result) Placeholder_StatusLine(context, 204);
    cont(result) Http_writeCRLF(context->s);
    return result;
})

ROUTER_CALLBACK(genericErrorCallback, {
    HttpStatusCode statusCode = context->statusCode;
    pure(result) Placeholder_StatusLine(context, statusCode);
    String content = mkString("<body><h1>Something very bad has happened</h1></body>");

    switch(statusCode) {
        case 400:
            printf("ERROR CODE %d\n", context->error);
            content = mkString("<html><body><h1>400 Bad Request</h1><h2>Your request is very bad (uncool and bad and not cool!) >:(</h2></body></html>");
            break;
        case 404:
            content = mkString("<html><body><h1>404 Not Found</h1><h2>Sorry we don't have this here</h2></body></html>");
            break;
        case 405:
            cont(result) flattenStreamResultWrite(stream_write(context->s, mkString("Allow: ")));
            bool written = false;
            for(int i = 0; context->allowedMethodMask; i++) {
                if(i != 0 && context->allowedMethodMask & 1) {
                    MaybeString s = Http_getMethod(i);
                    if(isJust(s)) {
                        if(written) {
                            cont(result) stream_writeChar(context->s, ',');
                            cont(result) stream_writeChar(context->s, ' ');
                        }
                        cont(result) flattenStreamResultWrite(stream_write(context->s, s.value));
                        written = true;
                    }
                }
                context->allowedMethodMask >>= 1;
            }
            cont(result) Http_writeCRLF(context->s);
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

    cont(result) Placeholder_AddContent(context, content);
    return result;
})

int main(int argc, char **argv) {
    argc = argc;
    argv = argv;

    ALLOC_PUSH(mkAlloc_LinearExpandable());

    int result;
    int sock = result = socket(AF_INET, SOCK_STREAM, 0);
    printf("SOCKET: %d\n", result);

    struct sockaddr_in addr = (struct sockaddr_in){
        .sin_family = AF_INET,
        .sin_port = htons(6969),
        .sin_addr = (struct in_addr){
            .s_addr = htonl(INADDR_ANY)
        },
    };

    result = bind(sock, (struct sockaddr *)&addr, sizeof(struct sockaddr_in));
    printf("BIND: %d\n", result);

    result = listen(sock, 128);
    printf("LISTEN: %d\n", result);

    Router router = (Router){
        .alloc = ALLOC,
        .routes = mkDynar(Route),

        .handler_routeNotFound  = mkHandler(genericErrorCallback),
        .handler_internalError  = mkHandler(genericErrorCallback),
        .handler_badRequest     = mkHandler(genericErrorCallback),
        .handler_notImplemented = mkHandler(genericErrorCallback),
    };

    FileStorage storage = { .alloc = ALLOC_GLOBAL, .hm = mkHashmap(ALLOC_GLOBAL) };
    storage.doHash = true;

    hm_fix(&storage.hm);
    FileTreeRouter ftrouter = mkFileTreeRouter(mkString("./dir"), &storage);

    addRoute(&router, GET, mkString("host"), mkString("/files/*"), mkHandlerArg(fileTreeCallback, memPointer(FileTreeRouter, &ftrouter)));
    addRoute(&router, GET, mkString("host"), mkString("/test"), mkHandlerArg(dataCallback, mkString("<body><h1>Test!</h1></body>")));
    addRoute(&router, POST, mkString("host"), mkString("/print"), mkHandler(printCallback));

    int i = 0;
    while(++i < 20) {
        struct sockaddr_in caddr = {0};
        socklen_t caddrLen = 0;
        int csock = accept(sock, (struct sockaddr *)&caddr, &caddrLen);
        printf("NEW CONNECTION\n");

        Connection _connection = {
            .addr = caddr,
            .clientSock = csock,
            .router = &router,
        };

        AllocateVarC(Connection, connection, _connection, ALLOC_GLOBAL);

        pthread_t thread;
        pthread_attr_t threadAttr;
        result = pthread_attr_init(&threadAttr);
        result = pthread_attr_setdetachstate(&threadAttr, PTHREAD_CREATE_DETACHED);
        result = pthread_create(&thread, &threadAttr, threadRoutine, connection);
        result = pthread_attr_destroy(&threadAttr);

        printf("CONNECTION: %d %d\n", csock, (int)thread);
    }

    int closeResult = close(sock);
    printf("CLOSE %d\n", closeResult);

    ALLOC_POP();
    close(sock);

    return 0;
}
