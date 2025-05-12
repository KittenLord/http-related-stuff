#ifndef __LIB_HTTP
#define __LIB_HTTP

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <http/uri.c>
#include <stream.h>
#include <map.h>
#include <text.h>

#define HTTP_CR 0x0D
#define HTTP_LF 0x0A

// NOTE: probably a good idea to split, like HttpHeaderError,
// HttpReqLineError, etc
typedef enum {
    HTTPERR_SUCCESS, // naming is my passion

    HTTPERR_INTERNAL_ERROR,
    HTTPERR_INVALID_METHOD,
    HTTPERR_UNKNOWN_METHOD,
    HTTPERR_INVALID_OPTIONS_TARGET,
    HTTPERR_REQUEST_LINE_ERROR,
    HTTPERR_INVALID_REQUEST_TARGET_PATH,
    HTTPERR_INVALID_FIELD_NAME,
    HTTPERR_INVALID_HEADER_FIELD,
    HTTPERR_INVALID_HEADER_FIELD_VALUE,
    HTTPERR_BAD_HOST,
    HTTPERR_REQUEST_TARGET_TOO_LONG,
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

    HTTP_CUSTOM
} HttpMethod;

typedef struct {
    u8 major;
    u8 minor;
    u16 value;
} HttpVersion;

typedef u16 HttpStatusCode;

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

// NOTE: Every header will have its string value on the same offset
typedef struct {
    String value;
// TODO: Ideally the name of this struct should be a reserved header
// that can't be used by its definition, but I couldn't find any like this
} HttpH_Unknown;

typedef struct {
    String value;
    Dynar(String) connectionOptions;
} HttpH_Connection;

typedef struct {
    String value;
    UriAuthority host; // NOTE: never has userinfo
} HttpH_Host;

typedef struct {
    String value;
    u64 contentLength;
} HttpH_ContentLength;

typedef struct {
    String name;
    String value;
} HttpParameter;
typedef struct {
    bool error;
    f32 q;
    Dynar(HttpParameter) list;
} HttpParameters;

typedef struct {
    String coding;
    HttpParameters params;
} HttpTransferCoding;
#define mkHttpTransferCoding(s) ((HttpTransferCoding){ .coding = mkString(s) })

typedef struct {
    String value;
    Dynar(HttpTransferCoding) codings;
} HttpH_TransferEncoding;
typedef HttpH_TransferEncoding HttpH_TE;
typedef HttpH_TransferEncoding HttpH_AcceptLanguage;

bool Http_writeDate(Stream *s) {
    time_t t = time(NULL);
    struct tm timeStamp;
    if(gmtime_r(&t, &timeStamp) != &timeStamp) return false;

    char *wdays[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    char *months[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    char *gmts = "GMT";
    Mem wday = mkMem(wdays[timeStamp.tm_wday], 3);
    Mem month = mkMem(months[timeStamp.tm_mon], 3);
    Mem gmt = mkMem(gmts, 3);

    pure(result) flattenStreamResultWrite(stream_write(s, wday));
    cont(result) stream_writeChar(s, ',');
    cont(result) stream_writeChar(s, ' ');

    cont(result) stream_writeChar(s, (timeStamp.tm_mday / 10) + '0');
    cont(result) stream_writeChar(s, (timeStamp.tm_mday % 10) + '0');
    cont(result) stream_writeChar(s, ' ');
    cont(result) flattenStreamResultWrite(stream_write(s, month));
    cont(result) stream_writeChar(s, ' ');
    // NOTE: will work for around 7000 years
    cont(result) decimalFromUNumber(s, timeStamp.tm_year + 1900);

    cont(result) stream_writeChar(s, ' ');

    cont(result) stream_writeChar(s, (timeStamp.tm_hour / 10) + '0');
    cont(result) stream_writeChar(s, (timeStamp.tm_hour % 10) + '0');
    cont(result) stream_writeChar(s, ':');
    cont(result) stream_writeChar(s, (timeStamp.tm_min / 10) + '0');
    cont(result) stream_writeChar(s, (timeStamp.tm_min % 10) + '0');
    cont(result) stream_writeChar(s, ':');
    cont(result) stream_writeChar(s, (timeStamp.tm_sec / 10) + '0');
    cont(result) stream_writeChar(s, (timeStamp.tm_sec % 10) + '0');

    cont(result) stream_writeChar(s, ' ');
    cont(result) flattenStreamResultWrite(stream_write(s, gmt));

    return result;
}

// TODO: Http_parseDate (not that we actually need it tbh, but for completeness's sake)

u16 Http_getVersion(u8 a, u8 b) {
    return ((u16)a << 8) | (u16)b;
}

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

// TODO: probably remove maxLen, we should limit the whole request body instead
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

    if(sb.len == 0) { return none(MaybeString); }

    String token = sb_build(sb);
    return just(MaybeString, token);
}

MaybeString Http_parseQuotedString(Stream *s, Alloc *alloc) {
    MaybeChar c = stream_peekChar(s);
    if(isNone(c) || c.value != '\"') return none(MaybeString);
    stream_popChar(s);

    StringBuilder sb = mkStringBuilder();
    sb.alloc = alloc;

    while(isJust(c = stream_popChar(s)) && c.value != '\"') {
        if(c.value == '\\') {
            MaybeChar next = stream_popChar(s);
            if(isNone(next)) return none(MaybeString);
            sb_appendChar(&sb, next.value);
        }
        else {
            sb_appendChar(&sb, c.value);
        }
    }

    String value = sb_build(sb);
    return just(MaybeString, value);
}

MaybeString Http_parseTokenOrQuotedString(Stream *s, Alloc *alloc) {
    MaybeChar c = stream_peekChar(s);
    if(isNone(c)) return none(MaybeString);
    if(c.value == '\"') return Http_parseQuotedString(s, alloc);
    else                return Http_parseToken(s, alloc, 0);
}

HttpParameters Http_parseParameters(Stream *s, Alloc *alloc) {
    HttpParameters result = {
        .error = false,
        .q = 1,
        .list = mkDynarA(HttpParameter, alloc),
    };

    MaybeChar c;
    while(isJust(c = stream_peekChar(s))) {
        Http_parseWS(s);

        c = stream_peekChar(s);
        // NOTE: hopefully we've encountered a comma, and not some junk
        if(isNone(c) || c.value != ';') return result;
        stream_popChar(s);

        Http_parseWS(s);

        c = stream_peekChar(s);
        if(isNone(c)) return none(HttpParameters);
        if(c.value == ';') continue; // for some stupid reason HTTP spec allows empty parameters

        MaybeString nameM = Http_parseToken(s, alloc, 0);
        if(isNone(nameM)) return none(HttpParameters);
        String name = nameM.value;
        toLower(name);

        c = stream_peekChar(s);
        if(isNone(c) || c.value != '=') return none(HttpParameters);
        stream_popChar(s);

        MaybeString valueM = Http_parseTokenOrQuotedString(s, alloc);
        if(isNone(valueM)) return none(HttpParameters);
        String value = valueM.value;

        if(mem_eq(name, mkString("q"))) {
            if(value.len > (1 + 1 + 3)) return none(HttpParameters);
            if(value.len == 0) return none(HttpParameters);

            f32 q = 0;
            f32 div = 1;
            for(int i = 0; i < value.len; i++) {
                if(i == 1 && value.s[i] != '.') return none(HttpParameters);
                if(i == 1) continue;

                if(!isDigit(value.s[i])) return none(HttpParameters);
                if(i == 0 && value.s[i] != '0' && value.s[i] != '1') return none(HttpParameters);

                q += (f32)(value.s[i] - '0') / div;
                div *= 10;
            }

            result.q = q;
        }
        else {
            HttpParameter param = {
                .name = name,
                .value = value,
            };

            dynar_append(&result.list, HttpParameter, param, _);
        }
    }

    return result;
}

bool Http_parseOne(Stream *s, byte c) {
    MaybeChar mc = stream_peekChar(s);
    if(isNone(mc) || mc.value != c) return false;
    stream_popChar(s);
    return true;
}

HttpMethod Http_parseMethod(Stream *s) {
    MaybeString mm = Http_parseToken(s, ALLOC, 20);
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
    else { return HTTPERR_UNKNOWN_METHOD; } // TODO: custom method handling
}

// FIXME: this is going to kill everything if we encounter CR without LF
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

bool Http_writeCRLF(Stream *s) {
    pure(result) stream_writeChar(s, HTTP_CR);
    cont(result) stream_writeChar(s, HTTP_LF);
    return result;
}

Http11RequestLine Http_parseHttp11RequestLine(Stream *s, Alloc *alloc) {
    HttpMethod method = Http_parseMethod(s);
    if(method == HTTP_INVALID_METHOD) { return fail(Http11RequestLine, HTTPERR_INVALID_METHOD); }

    bool result;

    result = Http_parseWS(s);
    if(!result) { return fail(Http11RequestLine, HTTPERR_REQUEST_LINE_ERROR); }

    MaybeChar c = stream_peekChar(s);
    if(isNone(c)) {
        return fail(Http11RequestLine, HTTPERR_REQUEST_LINE_ERROR);
    }

    // TODO: should we limit the request-line + headers instead? would solve a lot of problems
    stream_rlimitEnable(s, 8000);

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
        if(s->rlimit == 0) return fail(Http11RequestLine, HTTPERR_REQUEST_TARGET_TOO_LONG);
        if(isNone(path)) { return fail(Http11RequestLine, HTTPERR_INVALID_REQUEST_TARGET_PATH); }
        target.path = path;

        c = stream_peekChar(s);
        if(isJust(c) && c.value == '?') {
            stream_popChar(s);
            target.hasQuery = true;
            MaybeString query = Uri_parseQuery(s, alloc);

            if(s->rlimit == 0) return fail(Http11RequestLine, HTTPERR_REQUEST_TARGET_TOO_LONG);
            if(isNone(query)) { return fail(Http11RequestLine, HTTPERR_INVALID_REQUEST_TARGET_PATH); }
            target.query = query.value;
        }
    }
    else {
        // TODO: parse either uri authority, or an absolute-URI
    }

    stream_rlimitDisable(s);

    result = Http_parseWS(s);
    if(!result) { return fail(Http11RequestLine, HTTPERR_REQUEST_LINE_ERROR); }

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

    version.value = Http_getVersion(version.major, version.minor);

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

#define Http_generate_parseHeaderList(headerName, headerStr, listName, ty, parseSingle) \
HttpError Http_parseHeader_##headerName(Map *map, String value, HttpH_##headerName *already) { \
    Stream _s = mkStreamStr(value); \
    Stream *s = &_s; \
    StringBuilder sb = mkStringBuilder(); \
    Stream _out = mkStreamSb(&sb); \
    Stream *out = &_out; \
    pure(r) true; \
    if(already != null && already->value.len != 0) { \
        cont(r) flattenStreamResultWrite(stream_write(out, already->value)); \
        cont(r) stream_writeChar(out, ','); \
    } \
    cont(r) flattenStreamResultWrite(stream_write(out, value)); \
    if(!r) return HTTPERR_INTERNAL_ERROR; \
    HttpH_##headerName header = {0}; \
    header.listName = already == null ? mkDynarA(ty, map->alloc) : already->listName; \
    MaybeChar c; \
    while(isJust(c = stream_peekChar(s))) { \
        bool empty = false; \
        bool result = true; \
        ty value = {0}; \
        { parseSingle; } \
        if(!result) return HTTPERR_INVALID_HEADER_FIELD_VALUE; \
        if(!empty) { \
            bool result = true; \
            dynar_append(&header.listName, ty, value, result); \
            if(!result) return HTTPERR_INTERNAL_ERROR; \
        } \
        if(isNone(stream_peekChar(s))) break; \
        Http_parseWS(s); \
        bool r = Http_parseOne(s, ','); \
        if(!r) return HTTPERR_INVALID_HEADER_FIELD_VALUE; \
        Http_parseWS(s); \
    } \
    header.value = sb_build(sb); \
    map_set(map, mkString(headerStr), memPointer(HttpH_##headerName, &header)); \
    return HTTPERR_SUCCESS; \
}

Http_generate_parseHeaderList(Connection, "connection", connectionOptions, String, {
    MaybeString connectionOption = Http_parseToken(s, map->alloc, 0);
    // if(isNone(connectionOption)) {
    //     empty = true;
    // }
    {
        value = connectionOption.value;
    }
})

Http_generate_parseHeaderList(TE, "te", codings, HttpTransferCoding, {
    MaybeString coding = Http_parseToken(s, map->alloc, 0);
    cont(result) !coding.error;
    if(result) {
        HttpParameters params = Http_parseParameters(s, map->alloc);
        cont(result) !params.error;

        value = ((HttpTransferCoding){ .coding = coding.value, .params = params });
    }
})

// NOTE: testing
Http_generate_parseHeaderList(AcceptLanguage, "accept-language", codings, HttpTransferCoding, {
    MaybeString coding = Http_parseToken(s, map->alloc, 0);
    cont(result) isJust(coding);
    if(result) {
        HttpParameters params = Http_parseParameters(s, map->alloc);
        cont(result) isJust(params);

        value = ((HttpTransferCoding){ .coding = coding.value, .params = params });
    }
})

HttpError Http_parseHeader_Host(Map *map, String value, HttpH_Host *already) {
    if(already != null) return HTTPERR_BAD_HOST;
    Stream s = mkStreamStr(value);
    UriAuthority host = Uri_parseAuthorityWithoutUserinfo(&s, map->alloc);
    if(isNone(host)) return HTTPERR_INVALID_HEADER_FIELD_VALUE;
    HttpH_Host header = {
        .value = value,
        .host = host,
    };
    map_set(map, mkString("host"), memPointer(HttpH_Host, &header));
    return HTTPERR_SUCCESS;
}

HttpError Http_parseHeader_ContentLength(Map *map, String value, HttpH_ContentLength *already) {
    if(already != null) return HTTPERR_INVALID_HEADER_FIELD_VALUE;
    Stream s = mkStreamStr(value);

    u64 contentLength;
    bool result = unumberFromDecimal(&s, &contentLength, true);

    if(!result) return HTTPERR_INVALID_HEADER_FIELD_VALUE;

    HttpH_ContentLength header = {
        .value = value,
        .contentLength = contentLength,
    };

    map_set(map, mkString("content-length"), memPointer(HttpH_ContentLength, &header));
    return HTTPERR_SUCCESS;
}

HttpError Http_parseHeaderField(Stream *s, Map *map) {
    Alloc *alloc = map->alloc;
    
    // NOTE: I don't know any header longer than 64 chars lol
    MaybeString mfieldName = Http_parseToken(s, ALLOC, 64);
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
    HttpH_Unknown *already = memExtractPtr(HttpH_Unknown, map_get(map, fieldName));
    if(false) {}
    #define header(ty, str) else if(mem_eq(fieldName, mkString(str))) { return Http_parseHeader_##ty(map, fieldValue, (void *)already); }
    header(Connection, "connection")
    header(Host, "host")
    header(ContentLength, "content-length")
    header(TE, "te")
    header(AcceptLanguage, "accept-language")
    #undef header
    else {
        HttpH_Unknown header = { .value = fieldValue };
        map_setRepeat(map, fieldName, memPointer(HttpH_Unknown, &header));
        return HTTPERR_SUCCESS;
    }
}

String Http_getDefaultReasonPhrase(HttpStatusCode statusCode) {
    switch(statusCode) {
        // ======================
        // Informational 1xx
        // ======================
        case 100:
            return mkString("Continue");
        case 101:
            return mkString("Switching Protocols");

        // ======================
        // Successful 2xx
        // ======================
        case 200:
            return mkString("OK");
        case 201:
            return mkString("Created");
        case 202:
            return mkString("Accepted");
        case 203:
            return mkString("Non-Authoritative Information");
        case 204:
            return mkString("No Content");
        case 205:
            return mkString("Reset Content");
        case 206:
            return mkString("Partial Content");

        // ======================
        // Redirection 3xx
        // ======================
        case 300:
            return mkString("Multiple Choices");
        case 301:
            return mkString("Moved Permanently");
        case 302:
            return mkString("Found");
        case 303:
            return mkString("See Other");
        case 304:
            return mkString("Not Modified");
        // case 305:
        //     return mkString("Use Proxy");
        // case 306:
        //     return mkString("Unused");
        case 307:
            return mkString("Temporary Redirect");
        case 308:
            return mkString("Permanent Redirect");

        // ======================
        // Client Error 4xx
        // ======================
        case 400:
            return mkString("Bad Request");
        case 401:
            return mkString("Unauthorized");
        // case 402:
        //     return mkString("Payment Required");
        case 403:
            return mkString("Forbidden");
        case 404:
            return mkString("Not Found");
        case 405:
            return mkString("Method Not Allowed");
        case 406:
            return mkString("Not Acceptable");
        case 407:
            return mkString("Proxy Authentication Required");
        case 408:
            return mkString("Request Timeout");
        case 409:
            return mkString("Conflict");
        case 410:
            return mkString("Gone");
        case 411:
            return mkString("Length Required");
        case 412:
            return mkString("Precondition Failed");
        case 413:
            return mkString("Content Too Large");
        case 414:
            return mkString("URI Too Long");
        case 415:
            return mkString("Unsupported Media Type");
        case 416:
            return mkString("Range Not Satisfiable");
        case 417:
            return mkString("Expectation Failed");
        case 418:
            return mkString("I'm a teapot");
        case 421:
            return mkString("Misdirected Request");
        case 422:
            return mkString("Unprocessable Content");
        case 426:
            return mkString("Upgrade Required");

        // ======================
        // Server Error 5xx
        // ======================
        case 500:
            return mkString("Internal Server Error");
        case 501:
            return mkString("Not Implemented");
        case 502:
            return mkString("Bad Gateway");
        case 503:
            return mkString("Service Unavailable");
        case 504:
            return mkString("Gateway Timeout");
        case 505:
            return mkString("HTTP Version Not Supported");

        default:
            return mkString("dunno");
    }
}

bool Http_writeStatusLine(Stream *s, u8 major, u8 minor, HttpStatusCode statusCode, String reasonPhrase) {
    if(major > 9) return false;
    if(minor > 9) return false;
    if(statusCode < 100 || statusCode > 999) return false;

    if(isNull(reasonPhrase)) {
        reasonPhrase = Http_getDefaultReasonPhrase(statusCode);
    }

    pure(result) flattenStreamResultWrite(stream_write(s, mkString("HTTP/")));
    cont(result) stream_writeChar(s, major + '0');
    cont(result) stream_writeChar(s, '.');
    cont(result) stream_writeChar(s, minor + '0');

    cont(result) stream_writeChar(s, ' ');

    cont(result) decimalFromUNumber(s, statusCode);

    cont(result) stream_writeChar(s, ' ');

    cont(result) flattenStreamResultWrite(stream_write(s, reasonPhrase));

    cont(result) Http_writeCRLF(s);
    return result;
}

#endif // __LIB_HTTP
