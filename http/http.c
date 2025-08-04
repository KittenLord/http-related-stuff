#ifndef __LIB_HTTP
#define __LIB_HTTP

#include <time.h>

#include <stream.h>
#include <http/uri.c>
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
    HTTPERR_BAD_CONTENT_LENGTH,
    HTTPERR_BAD_TRANSFER_CODING,
    HTTPERR_UNKNOWN_TRANSFER_CODING,
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

#define GET_NO_HEAD     (1 << HTTP_GET)
#define HEAD            (1 << HTTP_HEAD)
#define GET             (GET_NO_HEAD | HEAD)
#define POST            (1 << HTTP_POST)
#define PUT             (1 << HTTP_PUT)
#define DELETE          (1 << HTTP_DELETE)
#define CONNECT         (1 << HTTP_CONNECT)
#define OPTIONS         (1 << HTTP_OPTIONS)
#define TRACE           (1 << HTTP_TRACE)
#define ANY             u64max
typedef u64 HttpMethodMask;

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
    u64 length;
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
    String type;
    bool typeWildcard;

    String subtype;
    bool subtypeWildcard;

    f32 q;

    HttpParameters params;
} HttpMediaType;
#define mkHttpMediaType(a, b) ((HttpMediaType){ .type = mkString(a), .typeWildcard = false, .subtype = mkString(b), .subtypeWildcard = false })
#define mkHttpMediaTypeX(a) ((HttpMediaType){ .type = mkString(a), .typeWildcard = false, .subtypeWildcard = true })
#define mkHttpMediaTypeXX() ((HttpMediaType){ .typeWildcard = true, .subtypeWildcard = true })

typedef struct {
    String name;
    MaybeString value;
} HttpChunkExtension;
typedef struct {
    bool error;
    Dynar(HttpChunkExtension) extensions;
} HttpChunkExtensions;

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
typedef HttpH_TransferEncoding HttpH_AcceptEncoding;

typedef struct {
    bool error;

    bool isWeak;
    String value;
} HttpEntityTag;

typedef struct {
    String value;

    bool any;
    Dynar(HttpEntityTag) etags;
} HttpH_IfMatch;
typedef HttpH_IfMatch HttpH_IfNotMatch;

typedef struct {
    String value;
    time_t lastModified;
} HttpH_IfModifiedSince;
typedef HttpH_IfModifiedSince HttpH_IfUnmodifiedSince;

bool Http_isMethodSafe(HttpMethod m) {
    return m == HTTP_GET
        || m == HTTP_HEAD
        || m == HTTP_OPTIONS
        || m == HTTP_TRACE;
}

bool Http_isMethodIdempotent(HttpMethod m) {
    return Http_isMethodSafe(m)
        || m == HTTP_PUT
        || m == HTTP_DELETE;
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

bool Http_parseOne(Stream *s, byte c) {
    MaybeChar mc = stream_peekChar(s);
    if(isNone(mc) || mc.value != c) return false;
    stream_popChar(s);
    return true;
}

HttpEntityTag Http_parseEntityTag(Stream *s, Alloc *alloc) {
    StringBuilder sb = mkStringBuilder();
    sb.alloc = alloc;

    bool isWeak = false;

    MaybeChar c = stream_peekChar(s);
    if(isNone(c)) return none(HttpEntityTag);
    if(c.value != 'W' && c.value != '\"') return none(HttpEntityTag);

    if(c.value == 'W') {
        stream_popChar(s);
        c = stream_peekChar(s);
        if(isNone(c) || c.value != '/') return none(HttpEntityTag);
        stream_popChar(s);
        isWeak = true;
    }

    c = stream_peekChar(s);
    if(isNone(c) || c.value != '\"') return none(HttpEntityTag);
    stream_popChar(s);

    while(isJust(c = stream_peekChar(s)) && c.value != '\"' && Http_isFieldVChar(c.value)) {
        sb_appendChar(&sb, c.value);
    }

    if(isNone(c)) return none(HttpEntityTag);
    if(c.value != '\"') return none(HttpEntityTag);
    
    return (HttpEntityTag){
        .isWeak = isWeak,
        .value = sb_build(sb),
    };
}

f32 Http_matchMediaType(HttpMediaType allowed, HttpMediaType testing) {
    if(allowed.typeWildcard && allowed.subtypeWildcard) return allowed.q;
    if(allowed.typeWildcard && mem_eq(allowed.subtype, testing.subtype)) return allowed.q;
    if(mem_eq(allowed.type, testing.type) && mem_eq(allowed.subtype, testing.subtype)) return allowed.q;
    return 0;
}

bool Http_writeMediaType(Stream *s, HttpMediaType mediaType) {
    if(mediaType.typeWildcard)      checkRet(stream_writeChar(s, '*'));
    else                            tryRet(stream_write(s, mediaType.type));

    checkRet(stream_writeChar(s, '/'));

    if(mediaType.subtypeWildcard)   checkRet(stream_writeChar(s, '*'));
    else                            tryRet(stream_write(s, mediaType.subtype));

    // TODO: write parameters
    // dynar_foreach()

    return true;
}

bool Http_writeDate(Stream *s, time_t t) {
    struct tm timeStamp;
    if(gmtime_r(&t, &timeStamp) != &timeStamp) return false;

    char *wdays[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    char *months[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    char *gmts = "GMT";
    Mem wday = mkMem(wdays[timeStamp.tm_wday], 3);
    Mem month = mkMem(months[timeStamp.tm_mon], 3);
    Mem gmt = mkMem(gmts, 3);

    tryRet(stream_write(s, wday));
    checkRet(stream_writeChar(s, ','));
    checkRet(stream_writeChar(s, ' '));

    checkRet(stream_writeChar(s, (timeStamp.tm_mday / 10) + '0'));
    checkRet(stream_writeChar(s, (timeStamp.tm_mday % 10) + '0'));
    checkRet(stream_writeChar(s, ' '));
    tryRet(stream_write(s, month));
    checkRet(stream_writeChar(s, ' '));
    // NOTE: will work for around 7000 years
    checkRet(writeU64ToDecimal(s, timeStamp.tm_year + 1900));

    checkRet(stream_writeChar(s, ' '));

    checkRet(stream_writeChar(s, (timeStamp.tm_hour / 10) + '0'));
    checkRet(stream_writeChar(s, (timeStamp.tm_hour % 10) + '0'));
    checkRet(stream_writeChar(s, ':'));
    checkRet(stream_writeChar(s, (timeStamp.tm_min / 10) + '0'));
    checkRet(stream_writeChar(s, (timeStamp.tm_min % 10) + '0'));
    checkRet(stream_writeChar(s, ':'));
    checkRet(stream_writeChar(s, (timeStamp.tm_sec / 10) + '0'));
    checkRet(stream_writeChar(s, (timeStamp.tm_sec % 10) + '0'));

    checkRet(stream_writeChar(s, ' '));
    tryRet(stream_write(s, gmt));

    return true;
}

bool Http_parseDate(Stream *s, time_t *t) {
    byte buffer[32];
    MaybeChar c;
    StringBuilder sb = mkStringBuilderMem(mkMem(buffer, 32));
    for(int i = 0; i < 3; i++) {
        c = stream_popChar(s);
        if(isNone(c)) return false;
        sb_appendChar(&sb, c.value);
    }

    char *wdays[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    char *months[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

    Mem wdaysBad[] = {
        mkString("Sunday"),
        mkString("Monday"),
        mkString("Tuesday"),
        mkString("Wednesday"),
        mkString("Thursday"),
        mkString("Friday"),
        mkString("Saturday"),
    };

    c = stream_peekChar(s);
    if(isNone(c)) return false;
    
    if(false){}
    // IMF-fixdate
    else if(c.value == ',') {
        int wday = 0;
        for(wday = 0; wday < 7; wday++) {
            if(mem_eq(sb_build(sb), mkMem(wdays[wday], 3))) break;
        }
        if(wday >= 7) return false;

        checkRet(Http_parseOne(s, ','));
        checkRet(Http_parseOne(s, ' '));

        u64 day = 0;
        checkRet(parseU64FromDecimalFixed(s, &day, 2, false));
        if(day >= 32) return false;

        checkRet(Http_parseOne(s, ' '));

        sb.len = 0;
        for(int i = 0; i < 3; i++) {
            c = stream_popChar(s);
            if(isNone(c)) return false;
            sb_appendChar(&sb, c.value);
        }
        int month = 0;
        for(month = 0; month < 12; month++) {
            if(mem_eq(sb_build(sb), mkMem(months[month], 3))) break;
        }
        if(month >= 12) return false;

        checkRet(Http_parseOne(s, ' '));

        u64 year = 0;
        checkRet(parseU64FromDecimalFixed(s, &year, 4, false));

        checkRet(Http_parseOne(s, ' '));

        u64 hour = 0;
        checkRet(parseU64FromDecimalFixed(s, &hour, 2, false));
        if(hour >= 24) return false;

        checkRet(Http_parseOne(s, ':'));

        u64 minute = 0;
        checkRet(parseU64FromDecimalFixed(s, &minute, 2, false));
        if(minute >= 60) return false;

        checkRet(Http_parseOne(s, ':'));

        u64 second = 0;
        checkRet(parseU64FromDecimalFixed(s, &second, 2, false));
        if(second >= 61) return false;

        checkRet(Http_parseOne(s, ' '));

        sb.len = 0;
        for(int i = 0; i < 3; i++) {
            c = stream_popChar(s);
            if(isNone(c)) return false;
            sb_appendChar(&sb, c.value);
        }

        if(!mem_eq(sb_build(sb), mkString("GMT"))) return false;

        struct tm timeStamp = {
            .tm_sec = second,
            .tm_min = minute,
            .tm_hour = hour,
            .tm_mday = day,
            .tm_mon = month,
            .tm_year = year,
            .tm_wday = wday,
        };
        time_t tr = timegm(&timeStamp);
        if(tr == -1) return false;
        *t = tr;

        return true;
    }
    // asctime
    else if(c.value == ' ') {
        int wday = 0;
        for(wday = 0; wday < 7; wday++) {
            if(mem_eq(sb_build(sb), mkMem(wdays[wday], 3))) break;
        }
        if(wday >= 7) return false;

        checkRet(Http_parseOne(s, ' '));

        sb.len = 0;
        for(int i = 0; i < 3; i++) {
            c = stream_popChar(s);
            if(isNone(c)) return false;
            sb_appendChar(&sb, c.value);
        }
        int month = 0;
        for(month = 0; month < 12; month++) {
            if(mem_eq(sb_build(sb), mkMem(months[month], 3))) break;
        }
        if(month >= 12) return false;

        checkRet(Http_parseOne(s, ' '));

        c = stream_peekChar(s);
        if(isNone(c)) return false;
        if(c.value != ' ' && !isDigit(c.value)) return false;
        stream_popChar(s);
        u8 day = c.value == ' ' ? 0 : (c.value - '0') * 10;
        c = stream_peekChar(s);
        if(isNone(c)) return false;
        if(!isDigit(c.value)) return false;
        stream_popChar(s);
        day += (c.value - '0');
        if(day >= 32) return false;

        checkRet(Http_parseOne(s, ' '));

        u64 hour = 0;
        checkRet(parseU64FromDecimalFixed(s, &hour, 2, false));
        if(hour >= 24) return false;

        checkRet(Http_parseOne(s, ':'));

        u64 minute = 0;
        checkRet(parseU64FromDecimalFixed(s, &minute, 2, false));
        if(minute >= 60) return false;

        checkRet(Http_parseOne(s, ':'));

        u64 second = 0;
        checkRet(parseU64FromDecimalFixed(s, &second, 2, false));
        if(second >= 61) return false;

        checkRet(Http_parseOne(s, ' '));

        u64 year = 0;
        checkRet(parseU64FromDecimalFixed(s, &year, 4, false));

        struct tm timeStamp = {
            .tm_sec = second,
            .tm_min = minute,
            .tm_hour = hour,
            .tm_mday = day,
            .tm_mon = month,
            .tm_year = year,
            .tm_wday = wday,
        };
        time_t tr = timegm(&timeStamp);
        if(tr == -1) return false;
        *t = tr;
        return true;
    }
    // rfc850-date
    else if(c.value == 'd' || c.value == 's' || c.value == 'n' || c.value == 'r' || c.value == 'u') {
        for(int i = 0; i < 10 && isJust(c) && c.value != ','; i++) {
            sb_appendChar(&sb, c.value);
            stream_popChar(s);
            c = stream_peekChar(s);
        }

        int wday = 0;
        for(wday = 0; wday < 7; wday++) {
            if(mem_eq(sb_build(sb), wdaysBad[wday])) break;
        }
        if(wday >= 7) return false;

        checkRet(Http_parseOne(s, ','));
        checkRet(Http_parseOne(s, ' '));

        u64 day = 0;
        checkRet(parseU64FromDecimalFixed(s, &day, 2, false));
        if(day >= 32) return false;

        checkRet(Http_parseOne(s, '-'));

        sb.len = 0;
        for(int i = 0; i < 3; i++) {
            c = stream_popChar(s);
            if(isNone(c)) return false;
            sb_appendChar(&sb, c.value);
        }
        int month = 0;
        for(month = 0; month < 12; month++) {
            if(mem_eq(sb_build(sb), mkMem(months[month], 3))) break;
        }
        if(month >= 12) return false;

        checkRet(Http_parseOne(s, '-'));

        u64 year = 0;
        checkRet(parseU64FromDecimalFixed(s, &year, 2, false));
        year += 2000;

        checkRet(Http_parseOne(s, ' '));

        u64 hour = 0;
        checkRet(parseU64FromDecimalFixed(s, &hour, 2, false));
        if(hour >= 24) return false;

        checkRet(Http_parseOne(s, ':'));

        u64 minute = 0;
        checkRet(parseU64FromDecimalFixed(s, &minute, 2, false));
        if(minute >= 60) return false;

        checkRet(Http_parseOne(s, ':'));

        u64 second = 0;
        checkRet(parseU64FromDecimalFixed(s, &second, 2, false));
        if(second >= 61) return false;

        checkRet(Http_parseOne(s, ' '));

        sb.len = 0;
        for(int i = 0; i < 3; i++) {
            c = stream_popChar(s);
            if(isNone(c)) return false;
            sb_appendChar(&sb, c.value);
        }

        if(!mem_eq(sb_build(sb), mkString("GMT"))) return false;

        struct tm timeStamp = {
            .tm_sec = second,
            .tm_min = minute,
            .tm_hour = hour,
            .tm_mday = day,
            .tm_mon = month,
            .tm_year = year,
            .tm_wday = wday,
        };
        time_t tr = timegm(&timeStamp);
        if(tr == -1) return false;
        *t = tr;

        return true;
    }
    else {
        return false;
    }
}

bool Http_writeDateNow(Stream *s) {
    return Http_writeDate(s, time(NULL));
}

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

HttpChunkExtensions Http_parseChunkExtensions(Stream *s, Alloc *alloc) {
    Dynar(HttpChunkExtension) extensions = mkDynarA(HttpChunkExtension, alloc);

    MaybeChar c;
    while(isJust(c = stream_peekChar(s))) {
        if(c.value != ';') return (HttpChunkExtensions){ .extensions = extensions };
        stream_popChar(s);

        MaybeString nameM = Http_parseToken(s, alloc, 0);
        if(isNone(nameM)) return none(HttpChunkExtensions);
        String name = nameM.value;

        c = stream_peekChar(s);
        if(isJust(c) && c.value == '=') {
            stream_popChar(s);

            MaybeString valueM = Http_parseTokenOrQuotedString(s, alloc);
            if(isNone(valueM)) return none(HttpChunkExtensions);
            String value = valueM.value;

            HttpChunkExtension ext = { .name = name, .value = just(MaybeString, value) };
            dynar_append(&extensions, HttpChunkExtension, ext, _);
        }
        else {
            HttpChunkExtension ext = { .name = name, .value = none(MaybeString) };
            dynar_append(&extensions, HttpChunkExtension, ext, _);
        }
    }

    return (HttpChunkExtensions){ .extensions = extensions };
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
            for(usz i = 0; i < value.len; i++) {
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
    else { return HTTP_CUSTOM; } // TODO: custom method handling
}

MaybeString Http_getMethod(HttpMethod method) {
    if(false) {}
    else if(method == HTTP_GET)         return just(MaybeString, mkString("GET"));
    else if(method == HTTP_HEAD)        return just(MaybeString, mkString("HEAD"));
    else if(method == HTTP_POST)        return just(MaybeString, mkString("POST"));
    else if(method == HTTP_PUT)         return just(MaybeString, mkString("PUT"));
    else if(method == HTTP_DELETE)      return just(MaybeString, mkString("DELETE"));
    else if(method == HTTP_CONNECT)     return just(MaybeString, mkString("CONNECT"));
    else if(method == HTTP_OPTIONS)     return just(MaybeString, mkString("OPTIONS"));
    else if(method == HTTP_TRACE)       return just(MaybeString, mkString("TRACE"));
    else return none(MaybeString);
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
    checkRet(stream_writeChar(s, HTTP_CR));
    checkRet(stream_writeChar(s, HTTP_LF));
    return true;
}

Http11RequestLine Http_parseHttp11RequestLine(Stream *s, Alloc *alloc) {
    HttpMethod method = Http_parseMethod(s);
    if(method == HTTP_INVALID_METHOD) { return fail(Http11RequestLine, HTTPERR_INVALID_METHOD); }
    if(method == HTTP_CUSTOM) { return fail(Http11RequestLine, HTTPERR_UNKNOWN_METHOD); }

    checkRetVal(Http_parseWS(s), fail(Http11RequestLine, HTTPERR_REQUEST_LINE_ERROR));

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

    checkRetVal(Http_parseWS(s), fail(Http11RequestLine, HTTPERR_REQUEST_LINE_ERROR));

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

    checkRetVal(Http_parseCRLF(s), fail(Http11RequestLine, HTTPERR_REQUEST_LINE_ERROR));

    return requestLine;
}

MaybeString Http_parseHeaderFieldValue(Stream *s, Alloc *alloc) {
    StringBuilder ws = mkStringBuilderCap(32);
    StringBuilder sb = mkStringBuilderCap(32);
    ws.alloc = alloc;
    sb.alloc = alloc;
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
    if(already != null && already->value.len != 0) { \
        tryRetVal(stream_write(out, already->value), HTTPERR_INTERNAL_ERROR); \
        checkRetVal(stream_writeChar(out, ','), HTTPERR_INTERNAL_ERROR); \
    } \
    tryRetVal(stream_write(out, value), HTTPERR_INTERNAL_ERROR); \
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
        checkRetVal(Http_parseOne(s, ','), HTTPERR_INVALID_HEADER_FIELD_VALUE); \
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

#define Http_generate_parseHeaderTranfer(_TE, str, onlyQ) \
Http_generate_parseHeaderList(_TE, str, codings, HttpTransferCoding, { \
    MaybeString coding = Http_parseToken(s, map->alloc, 0); \
    result = result && isJust(coding); \
    if(result) { \
        HttpParameters params = Http_parseParameters(s, map->alloc); \
        result = result && isJust(params); \
        if(onlyQ) { result = result && params.list.len == 0; } \
        value = ((HttpTransferCoding){ .coding = coding.value, .params = params }); \
    } \
})

Http_generate_parseHeaderTranfer(TE, "te", false)
Http_generate_parseHeaderTranfer(TransferEncoding, "transfer-encoding", false)
Http_generate_parseHeaderTranfer(AcceptEncoding, "accept-encoding", true)

#define Http_generate_parseHeaderIfMatch(_IfMatch, str) \
Http_generate_parseHeaderList(_IfMatch, str, etags, HttpEntityTag, { \
    if(Http_parseOne(s, '*')) { \
        result = result && already == null; \
        empty = true; \
        header.any = true; \
    } \
    else { \
        result = result && !header.any; \
        HttpEntityTag etag = Http_parseEntityTag(s, ALLOC); \
        result = result && isJust(etag); \
        value = etag; \
    } \
})

Http_generate_parseHeaderIfMatch(IfMatch, "if-match")
Http_generate_parseHeaderIfMatch(IfNotMatch, "if-not-match")

HttpError Http_parseHeader_Host(Map *map, String value, HttpH_Host *already) {
    if(already != null) return HTTPERR_BAD_HOST;
    Stream s = mkStreamStr(value);
    UriAuthority host = Uri_parseAuthorityWithoutUserinfo(&s, map->alloc);
    if(isNone(host)) return HTTPERR_INVALID_HEADER_FIELD_VALUE;
    if(isJust(stream_peekChar(&s))) return HTTPERR_INVALID_HEADER_FIELD_VALUE; // the stream should be exhausted
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

    u64 length;
    checkRetVal(parseU64FromDecimal(&s, &length, true), HTTPERR_INVALID_HEADER_FIELD_VALUE);

    HttpH_ContentLength header = {
        .value = value,
        .length = length,
    };

    map_set(map, mkString("content-length"), memPointer(HttpH_ContentLength, &header));
    return HTTPERR_SUCCESS;
}

// NOTE: per spec, we should actually ignore presence of multiple, as if there were none at all
#define Http_generate_parseHeaderModified(name, str) \
HttpError Http_parseHeader_##name(Map *map, String value, HttpH_##name *already) { \
    if(already != null) return HTTPERR_INVALID_HEADER_FIELD_VALUE; \
    Stream s = mkStreamStr(value); \
    time_t t; \
    checkRetVal(Http_parseDate(&s, &t), HTTPERR_INVALID_HEADER_FIELD_VALUE); \
    if(isJust(stream_peekChar(&s))) return HTTPERR_INVALID_HEADER_FIELD_VALUE; \
    HttpH_##name header = { \
        .value = value, \
        .lastModified = t, \
    }; \
    map_set(map, mkString(str), memPointer(HttpH_##name, &header)); \
    return HTTPERR_SUCCESS; \
}

Http_generate_parseHeaderModified(IfModifiedSince, "if-modified-since")
Http_generate_parseHeaderModified(IfUnmodifiedSince, "if-unmodified-since")

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

    HttpH_Unknown *already = memExtractPtr(HttpH_Unknown, map_get(map, fieldName));
    if(false) {}
    #define header(ty, str) else if(mem_eq(fieldName, mkString(str))) { return Http_parseHeader_##ty(map, fieldValue, (void *)already); }
    header(Connection, "connection")
    header(Host, "host")
    header(ContentLength, "content-length")
    header(TE, "te")
    header(TransferEncoding, "transfer-encoding")
    header(AcceptEncoding, "accept-encoding")
    header(IfMatch, "if-match")
    header(IfNotMatch, "if-not-match")
    header(IfModifiedSince, "if-modified-since")
    header(IfUnmodifiedSince, "if-unmodified-since")
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

    tryRet(stream_write(s, mkString("HTTP/")));
    checkRet(stream_writeChar(s, major + '0'));
    checkRet(stream_writeChar(s, '.'));
    checkRet(stream_writeChar(s, minor + '0'));

    checkRet(stream_writeChar(s, ' '));

    checkRet(writeU64ToDecimal(s, statusCode));

    checkRet(stream_writeChar(s, ' '));

    tryRet(stream_write(s, reasonPhrase));

    checkRet(Http_writeCRLF(s));
    return true;
}

#endif // __LIB_HTTP
