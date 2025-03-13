#include <stdio.h>
#include <stdlib.h>

#include <http/uri.c>
#include <stream.h>

typedef enum {
    HTTPERR_INVALID_METHOD,
    HTTPERR_INVALID_OPTIONS_TARGET,
    HTTPERR_REQUEST_LINE_ERROR,
    HTTPERR_INVALID_REQUEST_TARGET_PATH,
} HttpError;

typedef enum {
    HTTP_INVALID_METHOD,

    HTTP_GET,
    HTTP_HEAD,
    HTTP_POST,
    HTTP_PUT,
    HTTP_DELETE,
    HTTP_CONNECT,
    HTTP_OPTIONS,
    HTTP_TRACE,
} HttpMethod;

typedef struct {
    u8 major;
    u8 minor;
} HttpVersion;

typedef u8 Http11RequestTarget_Type;
#define HTTP11_REQUEST_TARGET_ORIGIN 0
#define HTTP11_REQUEST_TARGET_ABSOLUTE 1
#define HTTP11_REQUEST_TARGET_AUTHORITY 2
#define HTTP11_REQUEST_TARGET_ASTERISK 3
typedef struct {
    Http11RequestTarget_Type type;

    UriPath path;
    bool hasQuery;
    String query;

    Uri absolute;

    UriAuthority authority;
} Http11RequestTarget;

typedef struct {
    bool error;
    HttpError errmsg;

    HttpMethod method;
    HttpVersion version;

    Http11RequestTarget target;
} Http11RequestLine;

typedef struct {
    struct sockaddr_in addr;
    int clientSock;
} Connection;

bool Http_parseAny(Stream *s, String list) {
    MaybeChar c;
    bool success = false;
    while(isJust(c = stream_peekChar(s)) && string_contains(c.value, list)) {
        stream_popChar(s);
        success = true;
    }
    return success;
}

bool Http_isTokenChar(byte c) {
    return
    c == '!' || c == '#' ||
    c == '$' || c == '%' ||
    c == '&' || c == '\'' ||
    c == '*' || c == '+' ||
    c == '-' || c == '.' ||
    c == '^' || c == '_' ||
    c == '`' || c == '|' ||
    (c >= '0' && c <= '9') ||
    (c >= 'a' && c <= 'z') ||
    (c >= 'A' && c <= 'Z');
}

HttpMethod Http_parseMethod(Stream *s) {
    StringBuilder sb = mkStringBuilderCap(20);
    MaybeChar c;
    while(isJust(c = stream_peekChar(s)) && Http_isTokenChar(c.value)) {
        stream_popChar(s);
        if(!sb_appendChar(&sb, c.value)) {
            return HTTP_INVALID_METHOD;
        }
    }

    String m = sb_build(sb);

    if(false){}
    else if(mem_eq(mkString("GET"), m)) { return HTTP_GET; }
    else if(mem_eq(mkString("HEAD"), m)) { return HTTP_HEAD; }
    else if(mem_eq(mkString("POST"), m)) { return HTTP_POST; }
    else if(mem_eq(mkString("PUT"), m)) { return HTTP_PUT; }
    else if(mem_eq(mkString("DELETE"), m)) { return HTTP_DELETE; }
    else if(mem_eq(mkString("CONNECT"), m)) { return HTTP_CONNECT; }
    else if(mem_eq(mkString("OPTIONS"), m)) { return HTTP_OPTIONS; }
    else if(mem_eq(mkString("TRACE"), m)) { return HTTP_TRACE; }
    else { return HTTP_INVALID_METHOD; }
}

Http11RequestLine Http_parseHttp11RequestLine(Stream *s, Alloc *alloc) {
    // NOTE: As recommended by RFC9112
    stream_rlimitEnable(s, 8000);

    HttpMethod method = Http_parseMethod(s);
    if(method == HTTP_INVALID_METHOD) { return fail(Http11RequestLine, HTTPERR_INVALID_METHOD); }

    bool result;

    result = Http_parseAny(s, mkString(" \t"));
    if(!result) { return fail(Http11RequestLine, HTTP_INVALID_METHOD); }

    MaybeChar c = stream_peekChar(s);
    if(isNone(c)) {
        return fail(Http11RequestLine, HTTPERR_REQUEST_LINE_ERROR);
    }

    Http11RequestTarget target = {0};

    if(method == HTTP_OPTIONS) {
        target.type = HTTP11_REQUEST_TARGET_ASTERISK;

        if(c.value != '*') {
            return fail(Http11RequestLine, HTTPERR_INVALID_OPTIONS_TARGET);
        }

        stream_popChar(s);
    }
    else if(c.value == '/') {
        target.type = HTTP11_REQUEST_TARGET_ORIGIN;

        // parse uri path
        stream_popChar(s);
        UriPath path = Uri_parsePathRootlessOrEmpty(s, alloc);
        if(isNone(path)) { return fail(Http11RequestLine, HTTPERR_INVALID_REQUEST_TARGET_PATH); }
        target.path = path;

        c = stream_peekChar(s);
        if(isJust(c) && c.value == '?') {
            stream_popChar(s);
            target.hasQuery = true;
            MaybeString query = Uri_parseQuery(s, alloc);
            if(isNone(query)) { return fail(Http11RequestLine, HTTPERR_INVALID_REQUEST_TARGET_PATH); }
            target.query = query.value;
        }
    }
    else {
        // TODO: parse either uri authority, or an absolute-URI
    }

    result = Http_parseAny(s, mkString(" \t"));
    if(!result) { return fail(Http11RequestLine, HTTP_INVALID_METHOD); }

    HttpVersion version = {0};
    bool majorSet = false;
    String versionMask = mkString("HTTP/_._");
    for(int i = 0; i < versionMask.len; i++) {
        c = stream_popChar(s);
        if(isNone(c)) { return fail(Http11RequestLine, HTTPERR_REQUEST_LINE_ERROR); }
        if(versionMask.s[i] != '_' && c.value != versionMask.s[i]) { return fail(Http11RequestLine, HTTPERR_REQUEST_LINE_ERROR); }
        if(versionMask.s[i] != '_') continue;
        if(c.value >= '0' && c.value <= '9') {
            if(!majorSet) { majorSet = true; version.major = c.value - '0'; }
            else          { version.minor = c.value - '0'; }
        }
        else {
            return fail(Http11RequestLine, HTTPERR_REQUEST_LINE_ERROR);
        }
    }

    Http11RequestLine requestLine = {
        .method = method,
        .target = target,
        .version = version,
    };

    return requestLine;
}

