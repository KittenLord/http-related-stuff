#ifndef __LIB_HTTP
#define __LIB_HTTP

#include <stdio.h>
#include <stdlib.h>

#include <http/uri.c>
#include <stream.h>
#include <map.h>

#define HTTP_CR 0x0D
#define HTTP_LF 0x0A

typedef enum {
    HTTPERR_SUCCESS, // naming is my passion

    HTTPERR_INVALID_METHOD,
    HTTPERR_INVALID_OPTIONS_TARGET,
    HTTPERR_REQUEST_LINE_ERROR,
    HTTPERR_INVALID_REQUEST_TARGET_PATH,
    HTTPERR_INVALID_FIELD_NAME,
    HTTPERR_INVALID_HEADER_FIELD,
    HTTPERR_INVALID_HEADER_FIELD_VALUE,
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

bool Http_isWS(byte c) { return c == ' ' || c == '\t'; }
bool Http_parseWS(Stream *s) { return Http_parseAny(s, mkString(" \t")); }

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

MaybeString Http_parseToken(Stream *s, Alloc *alloc, isz maxLen) {
    StringBuilder sb = mkStringBuilderCap(maxLen <= 0 ? 32 : maxLen);
    sb.alloc = alloc;
    if(maxLen > 0) sb.dontExpand = true;

    MaybeChar c;
    while(isJust(c = stream_peekChar(s)) && Http_isTokenChar(c.value)) {
        stream_popChar(s);
        if(!sb_appendChar(&sb, c.value)) {
            return none(MaybeString);
        }
    }

    if(sb.s.len == 0) { return none(MaybeString); }

    String token = sb_build(sb);
    return just(MaybeString, token);
}

bool Http_parseOne(Stream *s, byte c) {
    MaybeChar mc = stream_peekChar(s);
    if(isNone(mc) || mc.value != c) return false;
    stream_popChar(s);
    return true;
}

HttpMethod Http_parseMethod(Stream *s) {
    MaybeString mm = Http_parseToken(s, &ALLOC, 20);
    if(isNone(mm)) { return HTTP_INVALID_METHOD; }
    String m = mm.value;

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

bool Http_parseCRLF(Stream *s) {
    MaybeChar c = stream_peekChar(s);
    if(isNone(c) || c.value != HTTP_CR) return false;
    stream_popChar(s);
    c = stream_peekChar(s);
    if(isNone(c) || c.value != HTTP_LF) {
        s->hasPeek = true;
        s->peekChar = HTTP_CR;
        return false;
    }

    stream_popChar(s);
    return true;
}

Http11RequestLine Http_parseHttp11RequestLine(Stream *s, Alloc *alloc) {
    // NOTE: As recommended by RFC9112
    stream_rlimitEnable(s, 8000);

    HttpMethod method = Http_parseMethod(s);
    if(method == HTTP_INVALID_METHOD) { return fail(Http11RequestLine, HTTPERR_INVALID_METHOD); }

    bool result;

    result = Http_parseWS(s);
    if(!result) { return fail(Http11RequestLine, HTTPERR_INVALID_METHOD); }

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

    result = Http_parseWS(s);
    if(!result) { return fail(Http11RequestLine, HTTPERR_INVALID_METHOD); }

    HttpVersion version = {0};
    bool majorSet = false;
    String versionMask = mkString("HTTP/_._");
    for(usz i = 0; i < versionMask.len; i++) {
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

    result = Http_parseCRLF(s);
    if(!result) { return fail(Http11RequestLine, HTTPERR_REQUEST_LINE_ERROR); }

    return requestLine;
}

bool Http_isObsText(byte c) {
    return c >= 0x80 /* && c <= 0xFF */ ;
}

bool Http_isVChar(byte c) {
    // https://datatracker.ietf.org/doc/html/rfc5234
    return c >= 0x21 && c <= 0x7E;
}

bool Http_isFieldVChar(byte c) {
    return Http_isVChar(c) || Http_isObsText(c);
}

MaybeString Http_parseHeaderFieldValue(Stream *s, Alloc *alloc) {
    StringBuilder ws = mkStringBuilderCap(32);
    StringBuilder sb = mkStringBuilderCap(32);
    bool flushWS = false;

    MaybeChar c;
    while(isJust(c = stream_peekChar(s)) && (Http_isFieldVChar(c.value) || Http_isWS(c.value))) {
        stream_popChar(s);
        if(Http_isFieldVChar(c.value)) {
            if(flushWS) {
                flushWS = false;
                sb_appendMem(&sb, sb_build(ws));
                sb_reset(&ws);
            }

            sb_appendChar(&sb, c.value);
        }
        else {
            flushWS = true;
            sb_appendChar(&ws, c.value);
        }
    }

    // reached unparseable character (presumably CRLF)
    // OWS after the field-value, as defined by the spec, is already consumed

    return just(MaybeString, sb_build(sb));
}

HttpError Http_parseHeaderField(Stream *s, Map *map) {
    Alloc *alloc = map->alloc;
    
    // NOTE: I don't know any header longer than 64 chars lol
    MaybeString mfieldName = Http_parseToken(s, &ALLOC, 64);
    if(isNone(mfieldName)) { return HTTPERR_INVALID_FIELD_NAME; }
    String fieldName = mfieldName.value;
    for(usz i = 0; i < fieldName.len; i++) {
        byte c = fieldName.s[i];
        if(c >= 'A' && c <= 'Z') { fieldName.s[i] = c - 'A' + 'a'; }
    }

    if(!Http_parseOne(s, ':')) { return HTTPERR_INVALID_HEADER_FIELD; }

    Http_parseWS(s);

    MaybeString mfieldValue = Http_parseHeaderFieldValue(s, alloc);
    if(isNone(mfieldValue)) { return HTTPERR_INVALID_HEADER_FIELD_VALUE; }
    String fieldValue = mfieldValue.value;

    bool alreadyIs = map_has(map, fieldName);
    if(!alreadyIs) {
        map_set(map, fieldName, fieldValue);
    }
    else {
        String oldValue = map_get(map, fieldName);
        StringBuilder newSb = mkStringBuilderCap(oldValue.len + 1 + fieldValue.len);
        sb_appendMem(&newSb, oldValue);
        sb_appendChar(&newSb, ',');
        sb_appendMem(&newSb, fieldValue);

        map_set(map, fieldName, sb_build(newSb));
    }

    return HTTPERR_SUCCESS;
}

String Http_getDefaultReasonPhrase(u16 statusCode) {
    return mkString("hello");
}

bool Http_writeStatusLine(Stream *s, u8 major, u8 minor, u16 statusCode, String reasonPhrase) {
    if(major > 9) return false;
    if(minor > 9) return false;
    if(statusCode < 100 || statusCode > 999) return false;

    if(reasonPhrase.s == null) {
        reasonPhrase = Http_getDefaultReasonPhrase(statusCode);
    }

    stream_write(s, mkString("HTTP/"));
    stream_writeChar(s, major + '0');
    stream_writeChar(s, '.');
    stream_writeChar(s, minor + '0');

    stream_writeChar(s, ' ');

    u8 s0 = statusCode / 100;
    u8 s1 = (statusCode % 100) / 10;
    u8 s2 = (statusCode % 10);
    stream_writeChar(s, s0 + '0');
    stream_writeChar(s, s1 + '0');
    stream_writeChar(s, s2 + '0');

    stream_writeChar(s, ' ');

    stream_write(s, reasonPhrase);

    stream_writeChar(s, HTTP_CR);
    stream_writeChar(s, HTTP_LF);

    stream_writeFlush(s);
    return true;
}

#endif // __LIB_HTTP
