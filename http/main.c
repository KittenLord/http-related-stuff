#include <stdio.h>
#include <stdlib.h>

#include <sys/socket.h>
#include <netinet/in.h>

#include <pthread.h>

#include <stream.h>

#include "http.c"

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
    printf("Method: %d\n", requestLine.method);
    printf("HTTP/%d.%d\n", requestLine.version.major, requestLine.version.minor);
    printf("Path: %d\n", requestLine.target.path.segmentCount);

    printf("helo\n");
    return null;
}

int main(int argc, char **argv) {
    int result;
    int sock = result = socket(AF_INET, SOCK_STREAM, 0);
    printf("SOCKET: %d\n", result);

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(6969),
        .sin_addr = htonl(INADDR_ANY)
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
