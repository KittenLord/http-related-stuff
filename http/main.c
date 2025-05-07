#include <stdio.h>
#include <stdlib.h>

#include <sys/socket.h>
#include <netinet/in.h>

#include <pthread.h>

#include <stream.h>
#include <compression/gzip.c>
#include <compression/zlib.c>

#include "http.c"
#include <text.h>

// NOTE: The issue was my lack of knowledge regarding detached/joinable
// threads, it's all pretty much perfect now
//
// https://askubuntu.com/a/1471201
//
// Reference command:
// systemd-run --scope -p MemoryMax=5M --user ../bin/http-testing

typedef struct {
    Stream *s;

    HttpVersion clientVersion;
    HttpMethod method;
    Map *headers;
    UriPath originalPath;
    UriPath relatedPath;
} RouteContext;

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

RoutePath BAD_ROUTE_PATH = { .error = true };

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

typedef struct Route Route;
struct Route {
    String subdomain;
    RoutePath path;

    RouteCallback *callback;
    Mem argument;

    // usz accessing;
    // pthread_mutex_t *lock;

    Route *prev;
    Route *next;
};

typedef struct {
    Alloc *alloc;

    Route *routes;
    // Route *routesDelete; // pediodically freed
    //
    // pthread_mutex_t *lock;
} Router;

typedef struct {
    Alloc *alloc;

    UriPath basePath;
} FileTreeRouter;

FileTreeRouter mkFileTreeRouter(String spath) {
    Alloc *alloc = ALLOC;
    Stream s = mkStreamStr(spath);
    UriPath path = Uri_parsePathRootless(&s, alloc);
    // if(isJust(stream_peekChar(&s))) {
    //     // NOTE: error
    // }

    FileTreeRouter ftrouter = {
        .alloc = alloc,
        .basePath = path,
    };

    return ftrouter;
}

Mem getFile(FileTreeRouter *ftrouter, UriPath subPath) {
    UriPath result = Uri_pathMoveRelatively(ftrouter->basePath, subPath, ALLOC);
    if(!Uri_pathHasPrefix(ftrouter->basePath, result)) return memnull;

    StringBuilder sb = mkStringBuilder();
    for(int i = 0; i < result.segments.len; i++) {
        sb_appendMem(&sb, dynar_index(String, &result.segments, i));
        if(i != result.segments.len - 1) {
            sb_appendChar(&sb, '/');
        }
    }

    String filePath = sb_build(sb);
    FILE *file = fopen(filePath.s, "r");
    if(file == null) return memnull;

    int fd = fileno(file);
    sb.len = 0;

    byte rbuffer[1024];
    byte wbuffer[1024];

    Stream fileStream = mkStreamFd(fd);
    stream_rbufferEnableC(&fileStream, mkMem(rbuffer, 1024));

    Stream resultStream = mkStreamSb(&sb);
    stream_wbufferEnableC(&resultStream, mkMem(wbuffer, 1024));

    MaybeChar c;
    while(isJust(c = stream_popChar(&fileStream))) {
        stream_writeChar(&resultStream, c.value);
    }
    
    stream_writeFlush(&resultStream);
    fclose(file);

    return sb_build(sb);
}

Router router;

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

Route *getRoute(Router *r, RouteContext *context) {
    Route *route = r->routes;
    while(route != null) {
        if(routePathMatches(route->path, context->relatedPath)) break;
        route = route->next;
    }

    if(route == null) return route;

    // TODO: we've found the route, fill the context
    // with all pattern matched path segments, chop
    // off the matched part of the path (for routes
    // that end with /*), etc

    RoutePath routePath = route->path;
    UriPath relatedPath = context->relatedPath;

    for(int i = 0; i < routePath.segments.len; i++) {
        RoutePathSegment rs = dynar_index(RoutePathSegment, &routePath.segments, i);
        if(rs.isWildcard) break;
        dynar_remove(String, &relatedPath.segments, 0);
    }

    context->relatedPath = relatedPath;

    return route;
}

void addRoute(Router *r, String host, String path, RouteCallback callback, Mem arg) {
    Stream s = mkStreamStr(path);
    RoutePath routePath = parseRoutePath(&s, r->alloc);

    // TODO: signal error
    if(isNone(routePath)) return;

    Route routeS = {
        .subdomain = host,
        .path = routePath,
        .callback = callback,
        .argument = arg,
    };

    AllocateVarC(Route, route, routeS, r->alloc);

    if(r->routes == null) {
        r->routes = route;
        return;
    }

    Route *parent = r->routes;
    while(parent->next != null) {
        parent = parent->next;
    }

    parent->next = route;
    route->prev = parent;
    return;
}

#define ROUTER_CALLBACK(name,  body) \
bool name(RouteContext *context, Mem arg) { body; }

#define ROUTER_CALLBACK_ARG(name, argty, argname, body) \
bool name(RouteContext *context, Mem arg) { argty* argname = (argty *)arg.s; { body; } }

ROUTER_CALLBACK(testCallback, {
    printf("helo\n");
    return false;
})

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
    cont(result) flattenStreamResultWrite(stream_writeFlush(context->s));

    return result;
}


ROUTER_CALLBACK_ARG(fileTreeCallback, FileTreeRouter, fileTree, {
    Mem file = getFile(fileTree, context->relatedPath);
    if(isNull(file)) {
        return false;
    }

    Http_writeStatusLine(context->s, 1, 1, 200, mkString("aboba"));

    pure(result) Placeholder_AddHeader(context, mkString("Connection"), mkString("keep-alive"));
    cont(result) Placeholder_AddContentLength(context, file.len);
    cont(result) Placeholder_AddContent(context, file);

    return true;
});

// NOTE: Non-blocking might actually be better (will try
// it out later [ideally I'll make the backend easily
// modifiable]), but I'm gonna do threads for now

void *threadRoutine(void *_connection) {
    Connection connection = *(Connection *)_connection;
    Free(_connection);

    Stream s = mkStreamFd(connection.clientSock);
    stream_wbufferEnable(&s, 4096);
    stream_rbufferEnable(&s, 4096);

    // TODO: kill the connection after a timeout
    while(true) {
        UseAlloc(mkAlloc_LinearExpandableA(ALLOC_GLOBAL), {
        // UseAlloc(*ALLOC_GLOBAL, {
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

            // TODO: we have the headers, including the Host, now
            // we reconstruct the target URI and parse it

            // TODO: with the target URI reconstructed, we can now,
            // *gulp*, convert the path+query back into a string to
            // feed to the router
            // Should I extend the Uri/UriPath structs to include a
            // string representation? I probably should

            RouteContext context = ((RouteContext){
                .s = &s,
                .clientVersion = requestLine.version,
                .method = requestLine.version,
                .headers = &headers,
                .originalPath = requestLine.target.path,

                // may be modified after getRoute()
                .relatedPath = requestLine.target.path,
            });

            Route *route = getRoute(&router, &context);
            if(route == null) {
                ALLOC_POP();
                goto cleanup;
            }

            route->callback(&context, route->argument);

            MapIter iter = map_iter(&headers);
            while(!map_iter_end(&iter)) {
                MapEntry entry = map_iter_next(&iter);
                // printf("HEADER NAME: %s\n", entry.key.s);
                // printf("HEADER VALUE: %s\n", entry.val.s);
                // printf("-----------\n");
            }
        });
    }

cleanup:
    Free(s.wbuffer.s);
    Free(s.rbuffer.s);
    close(connection.clientSock);

    return null;
}

int main(int argc, char **argv) {
    ALLOC_PUSH(mkAlloc_LinearExpandable());

    int result;
    int sock = result = socket(AF_INET, SOCK_STREAM, 0);
    printf("SOCKET: %d\n", result);

    struct sockaddr_in addr = (struct sockaddr_in){
        .sin_family = AF_INET,
        .sin_port = htons(6969),
        .sin_addr = htonl(INADDR_ANY),
    };
    result = bind(sock, (struct sockaddr *)&addr, sizeof(struct sockaddr_in));
    printf("BIND: %d\n", result);

    result = listen(sock, 128);
    printf("LISTEN: %d\n", result);

    // pthread_mutex_t routerLock = PTHREAD_MUTEX_INITIALIZER;
    router = (Router){
        .alloc = ALLOC,
        .routes = null,
        // .routesDelete = null,
        // .lock = &routerLock,
    };

    FileTreeRouter ftrouter = mkFileTreeRouter(mkString("./dir"));
    addRoute(&router, mkString("host"), mkString("/files/*"), fileTreeCallback, mkPointer(ftrouter));
    addRoute(&router, mkString("host"), mkString("/*"), testCallback, memnull);

    while(true) {
        struct sockaddr_in caddr = {0};
        socklen_t caddrLen = 0;
        int csock = accept(sock, (struct sockaddr *)&caddr, &caddrLen);
        printf("NEW CONNECTION\n");

        Connection _connection = {
            .addr = caddr,
            .clientSock = csock,
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

    ALLOC_POP();
    close(sock);

    return 0;
}
