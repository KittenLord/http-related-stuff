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

#define GET_NO_HEAD     (1 << HTTP_GET)
#define HEAD            (1 << HTTP_HEAD)
#define GET             (GET_NO_HEAD | HEAD)
#define POST            (1 << HTTP_POST)
#define PUT             (1 << HTTP_PUT)
#define DELETE          (1 << HTTP_DELETE)
#define CONNECT         (1 << HTTP_CONNECT)
#define OPTIONS         (1 << HTTP_OPTIONS)
#define TRACE           (1 << HTTP_TRACE)
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

typedef struct {
    bool error;

    HttpMethodMask methodMask;

    String subdomain;
    RoutePath path;

    RouteHandler handler;
} Route;

typedef struct {
    Alloc *alloc;

    Dynar(Route) routes;

    RouteHandler handler_generic400;
} Router;

struct RouteContext {
    Stream *s;

    HttpVersion clientVersion;
    HttpMethod method;
    Map *headers;
    UriPath originalPath;
    UriPath relatedPath;

    Router *mainRouter;
    Router *lastRouter;
};

typedef struct {
    bool error;

    Mem data;
    time_t modTime;
    Hash256 hash;
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

time_t getFileModTime(String path) {
    struct stat s = {0};
    int result = stat(path.s, &s);

    if(result != 0) return 0;
    return s.st_mtime;
}

File getFile(String path, Alloc *alloc) {
    printf("GET FILE\n");
    struct stat s = {0};
    int result = stat(path.s, &s);
    if(result != 0) return none(File);

    FILE *file = fopen(path.s, "r");
    if(file == null) return none(File);
    int fd = fileno(file);
    if(fd == -1) return none(File);

    Mem data = AllocateBytesC(alloc, s.st_size);
    isz bytesRead = read(fd, data.s, data.len);
    if(bytesRead != data.len) {
        FreeC(alloc, data.s);
        return none(File);
    }

    return (File){
        .data = data,
        .modTime = s.st_mtime,
    };
}

// TODO: error checking
void storageFillFile(File *file, FileStorage *storage) {
    if(storage->doHash) {
        file->hash = Sha256(file->data);
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

bool Placeholder_AddHeader(RouteContext *context, String header, String value) {
    pure(result) flattenStreamResultWrite(stream_write(context->s, header));
    cont(result) stream_writeChar(context->s, ':');
    cont(result) stream_writeChar(context->s, ' ');
    cont(result) flattenStreamResultWrite(stream_write(context->s, value));
    cont(result) stream_writeChar(context->s, HTTP_CR);
    cont(result) stream_writeChar(context->s, HTTP_LF);

    return result;
}

bool Placeholder_AddContentLength(RouteContext *context, u64 length) {
    pure(result) flattenStreamResultWrite(stream_write(context->s, mkString("Content-Length: ")));
    cont(result) decimalFromUNumber(context->s, length);
    cont(result) stream_writeChar(context->s, HTTP_CR);
    cont(result) stream_writeChar(context->s, HTTP_LF);

    return result;
}

bool Placeholder_AddContent(RouteContext *context, Mem content) {
    pure(result) stream_writeChar(context->s, HTTP_CR);
    cont(result) stream_writeChar(context->s, HTTP_LF);
    cont(result) flattenStreamResultWrite(stream_write(context->s, content));
    // cont(result) flattenStreamResultWrite(stream_writeFlush(context->s));

    return result;
}

void *threadRoutine(void *_connection) {
    Connection connection = *(Connection *)_connection;
    Free(_connection);

    Stream s = mkStreamFd(connection.clientSock);
    stream_wbufferEnable(&s, 4096);
    stream_rbufferEnable(&s, 4096);

    bool connectionPersists = false;

    // TODO: kill the connection after a timeout
    do {
        ALLOC_PUSH(mkAlloc_LinearExpandableA(ALLOC_GLOBAL));

        Http11RequestLine requestLine = Http_parseHttp11RequestLine(&s, ALLOC);

        Map headers = mkMap();
        while(!Http_parseCRLF(&s)) {
            HttpError result = Http_parseHeaderField(&s, &headers);
            bool crlf = Http_parseCRLF(&s);

            if(result != 0) {
                ALLOC_POP();
                goto cleanup;
            }
        }

        HttpH_Connection *connectionHeader = memExtractPtr(HttpH_Connection, map_get(&headers, mkString("connection")));
        bool containsClose = connectionHeader != null
            ? dynar_containsString(&connectionHeader->connectionOptions, mkString("close")) : false;
        bool containsKeepalive = connectionHeader != null
            ? dynar_containsString(&connectionHeader->connectionOptions, mkString("keep-alive")) : false;

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

        // TODO: if an error of any kind happens, we need a
        // trimmed down version of context that doesn't have
        // stuff like path (since we might have encountered
        // an error while parsing), etc
        RouteContext context = ((RouteContext){
            .s = &s,
            .clientVersion = requestLine.version,
            .method = requestLine.method,
            .headers = &headers,

            .originalPath = requestLine.target.path,
            .relatedPath = requestLine.target.path,

            .mainRouter = connection.router,
            .lastRouter = connection.router,
        });

        Route route = getRoute(connection.router, &context);
        if(isNone(route)) {
            ALLOC_POP();
            goto cleanup;
        }

        bool result = route.handler.callback(&context, route.handler.argument);
        stream_writeFlush(&s);

        MapIter iter = map_iter(&headers);
        while(!map_iter_end(&iter)) {
            MapEntry entry = map_iter_next(&iter);
            HttpH_Unknown header = memExtract(HttpH_Unknown, entry.val);
            String value = header.value;

            // printf("HEADER NAME: %.*s\n", entry.key.len, entry.key.s);
            // printf("HEADER VALUE: %.*s\n", value.len, value.s);
            // printf("-----------\n");
        }

        ALLOC_POP();
    } while(connectionPersists);

cleanup:
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
bool name(RouteContext *context, Mem arg) { context = context; arg = arg; argty* argname = (argty *)arg.s; { body; } }

ROUTER_CALLBACK(testCallback, {
    printf("helo\n");
    return false;
})

ROUTER_CALLBACK_ARG(fileTreeCallback, FileTreeRouter, fileTree, {
    File file = getFileTree(fileTree, context->relatedPath);
    if(isNone(file)) {
        return false;
    }

    pure(result) Http_writeStatusLine(context->s, 1, 1, 200, memnull);
    // cont(result) Placeholder_AddHeader(context, mkString("Connection"), mkString("keep-alive"));
    cont(result) Placeholder_AddContentLength(context, file.data.len);
    cont(result) Placeholder_AddContent(context, file.data);

    return result;
})

ROUTER_CALLBACK_STRING_ARG(fileCallback, filePath, {
    File file = getFile(filePath, ALLOC);
    if(isNone(file)) {
        return false;
    }

    pure(result) Http_writeStatusLine(context->s, 1, 1, 200, memnull);
    // cont(result) Placeholder_AddHeader(context, mkString("Connection"), mkString("keep-alive"));
    cont(result) Placeholder_AddContentLength(context, file.data.len);
    cont(result) Placeholder_AddContent(context, file.data);

    return result;
})

ROUTER_CALLBACK_STRING_ARG(dataCallback, data, {
    pure(result) Http_writeStatusLine(context->s, 1, 1, 200, memnull);
    cont(result) Placeholder_AddContentLength(context, data.len);
    cont(result) Placeholder_AddContent(context, data);
    return result;
})

int main(int argc, char **argv) {
    ALLOC_PUSH(mkAlloc_LinearExpandable());

    int result;
    int sock = result = socket(AF_INET, SOCK_STREAM, 0);
    printf("SOCKET: %d\n", result);

    struct sockaddr_in addr = (struct sockaddr_in){
        .sin_family = AF_INET,
        .sin_port = htons(6969),
        .sin_addr = htonl(INADDR_ANY)
    };
    result = bind(sock, (struct sockaddr *)&addr, sizeof(struct sockaddr_in));
    printf("BIND: %d\n", result);

    result = listen(sock, 128);
    printf("LISTEN: %d\n", result);

    Router router = (Router){
        .alloc = ALLOC,
        .routes = mkDynar(Route),

        .handler_generic400 = mkHandlerArg(dataCallback, mkString("<body><h1>your request is very bad i dont like it</h1></body>")),
    };

    FileStorage storage = { .alloc = ALLOC_GLOBAL, .hm = mkHashmap(ALLOC_GLOBAL) };
    hm_fix(&storage.hm);
    FileTreeRouter ftrouter = mkFileTreeRouter(mkString("./dir"), &storage);

    addRoute(&router, GET, mkString("host"), mkString("/files/*"), mkHandlerArg(fileTreeCallback, memPointer(FileTreeRouter, &ftrouter)));
    addRoute(&router, GET, mkString("host"), mkString("/test"), mkHandlerArg(dataCallback, mkString("<body><h1>Test!</h1></body>")));
    addRoute(&router, GET, mkString("host"), mkString("/*"), mkHandler(testCallback));

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

        printf("CONNECTION: %d %d\n", csock, thread);
    }

    int closeResult = close(sock);
    printf("CLOSE %d\n", closeResult);

    ALLOC_POP();
    close(sock);

    return 0;
}
