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
} RouteContext;

typedef bool (*RouteCallback)(RouteContext *, Mem);

typedef struct Route Route;
struct Route {
    String path;

    RouteCallback callback;
    Mem argument;

    usz accessing;
    pthread_mutex_t *lock;

    Route *prev;
    Route *next;
};

typedef struct {
    Alloc *alloc;

    Route *routes;
    Route *routesDelete; // pediodically freed

    pthread_mutex_t *lock;
} Router;

Route *getRoute(Router *router, String path) {
    pthread_mutex_lock(router->lock);

    Route *route = router->routes;
    while(route != null) {
        if(mem_eq(route->path, path)) { break; }
        route = route->next;
    }

    if(route != null) {
        pthread_mutex_lock(route->lock);
        route->accessing += 1;
        pthread_mutex_unlock(route->lock);
    }

    pthread_mutex_unlock(router->lock);
    return route;
}

void freeRoute(Router *router, Route *route) {
    if(router == null) return;
    if(route == null) return;
    FreeC(router->alloc, route->path.s);
    FreeC(router->alloc, route->argument.s);
    FreeC(router->alloc, route->lock);
}

bool deleteRoute(Router *router, String path) {
    pthread_mutex_lock(router->lock);

    Route *route = router->routes;
    while(route != null) {
        if(mem_eq(route->path, path)) { break; }
        route = route->next;
    }

    if(route == null) { return false; }

    pthread_mutex_lock(route->lock);
    if(route->prev) route->prev->next = route->next;
    if(route->next) route->next->prev = route->prev;
    if(route->accessing > 0) {
        route->next = router->routesDelete;
        router->routesDelete = route;
    }
    else {
        freeRoute(router, route);
    }
    pthread_mutex_unlock(route->lock);

    pthread_mutex_unlock(router->lock);
    return true;
}

void addRoute(Router *router, bool replace, String path, RouteCallback callback, Mem argument) {
    pthread_mutex_lock(router->lock);

    Route *route = router->routes;
    bool found = false;
    while(route != null && route->next != null) {
        if(mem_eq(route->path, path)) { found = true; break; }
        route = route->next;
    }

    if(found && !replace) {
        pthread_mutex_unlock(router->lock);
        return;
    }

    AllocateVarC(Route, newRoute, ((Route){0}), router->alloc);
    Route *prev = route->prev;
    Route *next = route->next;

    if(found) {
        newRoute->prev = prev;
        newRoute->next = next;

        pthread_mutex_lock(route->lock);
        if(prev) prev->next = newRoute;
        if(next) next->prev = newRoute;
        if(route->accessing > 0) {
            route->next = router->routesDelete;
            router->routesDelete = route;
        }
        else {
            freeRoute(router, route);
        }
        pthread_mutex_unlock(route->lock);
    }
    else {
        route->next = newRoute;
        newRoute->prev = route;
    }

    argument = mem_clone(argument, router->alloc);
    path = mem_clone(path, router->alloc);

    newRoute->path = path;
    newRoute->callback = callback;
    newRoute->argument = argument;
    newRoute->accessing = 0;

    // NOTE: I think `fastmutex` is what I think it is
    AllocateVarC(pthread_mutex_t, newRouteLock, ((pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER), router->alloc);
    newRoute->lock = newRouteLock;

    pthread_mutex_unlock(router->lock);
}

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

    MapIter *iter = map_iter(&headers);
    while(!map_iter_end(iter)) {
        printf("HEADER NAME: %s\n", iter->key.s);
        printf("HEADER VALUE: %s\n", iter->val.s);
        printf("-----------\n");
        iter = map_iter_next(iter);
    }

    Http_writeStatusLine(&s, 1, 1, 404, mkString("kill yourself"));
    stream_write(&s, mkString("Content-Length: 5"));
    stream_writeChar(&s, HTTP_CR);
    stream_writeChar(&s, HTTP_LF);
    stream_writeChar(&s, HTTP_CR);
    stream_writeChar(&s, HTTP_LF);
    stream_write(&s, mkString("helo!"));
    stream_writeFlush(&s);

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

    ALLOC_INDEX = 69;

    while(true) {
        struct sockaddr_in caddr = {0};
        socklen_t caddrLen = 0;
        int csock = accept(sock, (struct sockaddr *)&caddr, &caddrLen);

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
