#include <stdio.h>
#include <stdlib.h>

#include <sys/socket.h>
#include <netinet/in.h>

#include <pthread.h>

#include <stream.h>

#include "http.c"

// NOTE: I suspect the current router system (lock for router
// and per route) is horribly inefficient, but I'll return to
// this later. Right now I just want to make a functional
// prototype

typedef struct {
    Stream *s;
    Map *headers;
    UriPath originalPath;
    UriPath relatedPath;
} RouteContext;

typedef bool (RouteCallback)(RouteContext *, Mem);

typedef struct RoutePath RoutePath;
struct RoutePath {
    bool error;

    String segment;
    bool isMatch; // match a single segment
    bool isWildcard; // match all (or none) remaining segments

    RoutePath *next;
};

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

RoutePath *parseRoutePath(Stream *s, Alloc *alloc) {
    MaybeChar c = stream_peekChar(s);
    if(isNone(c)) return null;
    if(c.value != '/') return &BAD_ROUTE_PATH;
    stream_popChar(s);

    c = stream_peekChar(s);
    if(isNone(c)) return null;

    RoutePath segment = {0};

    // TODO: allow for escaped '{' and '}'

    if(c.value == '{') {
        MaybeString matchSegment = parseRoutePathMatch(s, alloc);
        if(isNone(matchSegment)) return &BAD_ROUTE_PATH;

        segment.segment = matchSegment.value;
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

        segment.segment = sb_build(sb);
    }

    RoutePath *next = parseRoutePath(s, alloc);
    if(next != null && isNone(*next)) return &BAD_ROUTE_PATH;

    segment.next = next;

    if(next == null && segment.segment.len == 1 && segment.segment.s[0] == '*') {
        segment.isWildcard = true;
    }

    AllocateVarC(RoutePath, ret, segment, alloc);
    return ret;
}

typedef struct Route Route;
struct Route {
    String subdomain;
    RoutePath *path;

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
    UriPathSegment *segment = result.segments;
    while(segment != null) {
        sb_appendMem(&sb, segment->segment);
        if(segment->next != null) {
            sb_appendChar(&sb, '/');
        }

        segment = segment->next;
    }

    FILE *file = fopen(sb_build(sb).s, "r");
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

    return sb_build(sb);
}

Router router;

bool routePathMatches(RoutePath *routePath, UriPath uriPathS) {
    UriPathSegment *uriPath = uriPathS.segments;

    while(uriPath != null && routePath != null) {
        printf("URI: %s, ROUTE: %s\n", uriPath->segment.s, routePath->segment.s);

        if(routePath->isWildcard) return true; // all previous segments matched

        if(!routePath->isMatch && !mem_eq(routePath->segment, uriPath->segment)) {
            return false;
        }

        uriPath = uriPath->next;
        routePath = routePath->next;
    }

    if(uriPath == null && routePath != null && routePath->isWildcard) {
        return true;
    }

    if(routePath == null && (uriPath == null || (uriPath->segment.len == 0 && uriPath->next == null))) {
        // last empty segment is ignored
        return true;
    }

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

    RoutePath *routePath = route->path;
    UriPath relatedPath = context->relatedPath;

    while(routePath != null) {
        if(routePath->isWildcard) {
            break;
        }

        routePath = routePath->next;
        relatedPath.segments = relatedPath.segments->next;
        relatedPath.segmentCount -= 1;
    }

    context->relatedPath = relatedPath;

    return route;
}

void addRoute(Router *r, String host, String path, RouteCallback callback, Mem arg) {
    Stream s = mkStreamStr(path);
    RoutePath *routePath = parseRoutePath(&s, r->alloc);

    // TODO: signal error
    if(routePath == &BAD_ROUTE_PATH) return;

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

ROUTER_CALLBACK_ARG(fileTreeCallback, FileTreeRouter, fileTree, {
    Mem file = getFile(fileTree, context->relatedPath);
    if(file.s == null) {
        printf("BAD FILE\n");
        return false;
    }

    printf("GOOD FILE\n");

    // printf("FILE READ: %s\n", file.s);

    Http_writeStatusLine(context->s, 1, 1, 200, mkString("aboba"));
    stream_write(context->s, mkString("Content-Length: "));

    byte contentLength[32] = {0};
    sprintf(contentLength, "%d", file.len);
    stream_write(context->s, mkString(contentLength));

    stream_writeChar(context->s, HTTP_CR);
    stream_writeChar(context->s, HTTP_LF);
    stream_writeChar(context->s, HTTP_CR);
    stream_writeChar(context->s, HTTP_LF);
    stream_write(context->s, file);
    stream_writeFlush(context->s);

    return true;
});

// NOTE: Non-blocking might actually be better (will try
// it out later [ideally I'll make the backend easily
// modifiable]), but I'm gonna do threads for now

void *threadRoutine(void *_connection) {
    Connection connection = *(Connection *)_connection;
    FreeC(ALLOC_GLOBAL, _connection);

    Stream s = mkStreamFd(connection.clientSock);
    stream_wbufferEnable(&s, 4096);
    stream_rbufferEnable(&s, 4096);

    Http11RequestLine requestLine = Http_parseHttp11RequestLine(&s, ALLOC_GLOBAL);
    Map headers = mkMapA(ALLOC_GLOBAL);
    while(!Http_parseCRLF(&s)) {
        HttpError result = Http_parseHeaderField(&s, &headers);
        bool crlf = Http_parseCRLF(&s);

        printf("ERROR: %d\n", result);
    }

    // TODO: we have the headers, including the Host, now
    // we reconstruct the target URI and parse it

    // TODO: with the target URI reconstructed, we can now,
    // *gulp*, convert the path+query back into a string to
    // feed to the router
    // Should I extend the Uri/UriPath structs to include a
    // string representation? I probably should

    RouteContext context = {
        .s = &s,
        .headers = &headers,
        .originalPath = requestLine.target.path,
        .relatedPath = requestLine.target.path,
    };

    Route *route = getRoute(&router, &context);
    if(route == null) {
        printf("invalid route\n");
        return null;
    }

    route->callback(&context, route->argument);

    MapIter *iter = map_iter(&headers);
    while(!map_iter_end(iter)) {
        printf("HEADER NAME: %s\n", iter->key.s);
        printf("HEADER VALUE: %s\n", iter->val.s);
        printf("-----------\n");
        iter = map_iter_next(iter);
    }

    // Http_writeStatusLine(&s, 1, 1, 404, mkString("aboba"));
    // stream_write(&s, mkString("Content-Length: 5"));
    // stream_writeChar(&s, HTTP_CR);
    // stream_writeChar(&s, HTTP_LF);
    // stream_writeChar(&s, HTTP_CR);
    // stream_writeChar(&s, HTTP_LF);
    // stream_write(&s, mkString("helo!"));
    // stream_writeFlush(&s);

    return null;
}

int main(int argc, char **argv) {
    int result;
    int sock = result = socket(AF_INET, SOCK_STREAM, 0);
    printf("SOCKET: %d\n", result);

    struct sockaddr_in addr = {
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
        .alloc = ALLOC_GLOBAL,
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
        result = pthread_create(&thread, &threadAttr, threadRoutine, connection);
    }

    return 0;
}
