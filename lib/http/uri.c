#ifndef __LIB_URI
#define __LIB_URI

// Based on RFC-3986
// https://datatracker.ietf.org/doc/html/rfc3986

// NOTE: If I got it correctly, the RFC3986 only permits the character set that is used in US-ASCII, and everything else is to be encoded using percent-encodings, so this implementation uses char everywhere (as opposed to runes)

#include <types.h>
#include <str.h>
#include <macros.h>
#include <dynar.h>

typedef struct {
    byte a;
    byte b;
    byte c;
    byte d;
} UriIpv4;

typedef struct {

} UriIpLiteral;

typedef u8 UriHostType;
#define URI_HOST_IPLITERAL 1
#define URI_HOST_IPV4 2
#define URI_HOST_REGNAME 3
typedef struct {
    bool error;
    String errmsg;

    UriHostType type;

    union {
        String regName;
        UriIpv4 ipv4;
        UriIpLiteral ipLiteral;
    };
} UriHost;

typedef struct {
    bool error;
    String errmsg;

    bool hasUserInfo;
    String userInfo;

    UriHost host;

    bool hasPort;
    String portString;
    u64 port;
    bool portOverflow;
} UriAuthority;

typedef u8 UriPathType;
#define URI_PATH_EMPTY 0
#define URI_PATH_ABEMPTY 1
#define URI_PATH_ABSOLUTE 2
#define URI_PATH_NOSCHEME 3
#define URI_PATH_ROOTLESS 4
typedef struct {
    bool error;
    String errmsg;

    UriPathType type;

    Dynar(String) segments;
} UriPath;

typedef u8 UriHierarchyPartType;
#define URI_HIER_EMPTY 0
#define URI_HIER_AUTHORITY 1
#define URI_HIER_ABSOLUTE 2
#define URI_HIER_ROOTLESS 3
typedef struct {
    bool error;
    String errmsg;

    UriHierarchyPartType type;

    bool hasAuthority;
    UriAuthority authority;

    bool hasPath;
    UriPath path;
} UriHierarchyPart;

typedef struct {
    bool error;
    String errmsg;

    String scheme;
    UriHierarchyPart hierarchyPart;

    bool hasQuery;
    String query;

    bool hasFragment;
    String fragment;
} Uri;

bool Uri_isGenericDelimiter(byte c) {
    return
    c == ':' ||
    c == '/' ||
    c == '?' ||
    c == '#' ||
    c == '[' ||
    c == ']' ||
    c == '@' ;
}

bool Uri_isSubcomponentDelimiter(byte c) {
    return
    c == '!' ||
    c == '$' || 
    c == '&' ||
    c == '\''||
    c == '(' ||
    c == ')' ||
    c == '*' ||
    c == '+' ||
    c == ',' ||
    c == ';' ||
    c == '=' ;
}

bool Uri_isDigit(byte c) {
    return c >= '0' && c <= '9';
}

bool Uri_isAlpha(byte c) {
    return
    (c >= 'a' && c <= 'z') ||
    (c >= 'A' && c <= 'Z') ;
}

bool Uri_isSchemeChar(byte c) {
    return
    Uri_isAlpha(c) ||
    Uri_isDigit(c) ||
    c == '+'       ||
    c == '-'       ||
    c == '.'       ;
}

bool Uri_isUnreserved(byte c) {
    return
    (c >= 'a' && c <= 'z') ||
    (c >= 'A' && c <= 'Z') ||
    (c >= '0' && c <= '9') ||
    (c == '-'            ) ||
    (c == '.'            ) ||
    (c == '_'            ) ||
    (c == '~'            ) ;
}

bool Uri_isHexdigit(byte c) {
    return
    (c >= '0' && c <= '9') ||
    (c >= 'a' && c <= 'f') ||
    (c >= 'A' && c <= 'F') ;
}

bool Uri_getHexDigitValue(byte c) {
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'a' && c <= 'f') return c - 'a' + 10;
    if(c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

byte Uri_normalizePercentByte(byte c) {
    if(c >= 'a' && c <= 'f') return c - 'a' + 'A';
    return c;
}

byte Uri_lowercaseChar(byte c) {
    if(c >= 'A' && c <= 'Z') return c - 'A' + 'a';
    return c;
}

void Uri_lowercase(String *s) {
    if(!s) return;
    for(usz i = 0; i < s->len; i++) {
        s->s[i] = Uri_lowercaseChar(s->s[i]);
    }
}

// NOTE: a lot of productions use this instead of an actual
// pchar, and pchar is a superset of this, so might as well
// define it separately
bool Uri_isPcharRaw(byte c, String extra) {
    return
    Uri_isUnreserved(c)            ||
    Uri_isSubcomponentDelimiter(c) ||
    c == '%'                       ||
    string_contains(c, extra)      ;
}

bool Uri_isPchar(byte c) {
    return Uri_isPcharRaw(c, mkString("@:"));
}

String Uri_ipv4ToString(UriIpv4 ip, Alloc *alloc) {
    StringBuilder sb = mkStringBuilderCap(16);
    sb.alloc = alloc;

    byte *arr[] = { &ip.a, &ip.b, &ip.c, &ip.d };
    for(int i = 0; i < 4; i++) {
        byte current = *(arr[i]);
        bool metNonZero = false;

        for(int div = 100; div > 0; div /= 10) {
            byte digit = current / div;
            current -= div * digit;
            if(metNonZero || div == 1 || digit != 0) metNonZero = true;
            if(digit == 0 && !metNonZero) continue;
            sb_appendChar(&sb, digit + '0');
        }

        if(i != 3) { sb_appendChar(&sb, '.'); }
    }

    return sb_build(sb);
}

MaybeString Uri_parseScheme(Stream *s, Alloc *alloc) {
    // TODO: figure out a good approach to this
    static String error_failedToReadChar = mkString("Failed to read a character" DEBUG_LOC);
    static String error_firstCharAlpha = mkString("First character of the scheme must be a letter" DEBUG_LOC);

    StringBuilder sb = mkStringBuilderCap(16);
    sb.alloc = alloc;

    MaybeChar c = stream_peekChar(s);

    if(isNone(c)) return fail(MaybeString, (u64)&error_failedToReadChar);
    if(isJust(c) && !Uri_isAlpha(c.value)) return fail(MaybeString, (u64)&error_firstCharAlpha);

    sb_appendChar(&sb, c.value);
    stream_popChar(s);

    while(isJust(c = stream_peekChar(s)) && Uri_isSchemeChar(c.value)) {
        sb_appendChar(&sb, c.value);
        stream_popChar(s);
    }

    return just(MaybeString, sb_build(sb));
}

#define PCHAR_INVALID_PERCENT_ENCODING 1
MaybeString Uri_parsePcharRaw(Stream *s, Alloc *alloc, bool lowercase, String extra) {
    MaybeChar c = stream_peekChar(s);
    if(isNone(c)) return none(MaybeString);

    StringBuilder sb = mkStringBuilderCap(3);
    sb.alloc = alloc;
    if(lowercase) c.value = Uri_lowercaseChar(c.value);
    sb_appendChar(&sb, c.value);


    if(c.value != '%' && Uri_isPcharRaw(c.value, extra)) {
        stream_popChar(s);
        return just(MaybeString, sb_build(sb));
    }

    if(c.value != '%') return none(MaybeString);

    stream_popChar(s);

    byte p = 0;

    c = stream_peekChar(s);
    if(isNone(c) || (isJust(c) && !Uri_isHexdigit(c.value))) return fail(MaybeString, PCHAR_INVALID_PERCENT_ENCODING);
    sb_appendChar(&sb, Uri_normalizePercentByte(c.value));
    stream_popChar(s);
    p += 16*Uri_getHexDigitValue(c.value);

    c = stream_peekChar(s);
    if(isNone(c) || (isJust(c) && !Uri_isHexdigit(c.value))) return fail(MaybeString, PCHAR_INVALID_PERCENT_ENCODING);
    sb_appendChar(&sb, Uri_normalizePercentByte(c.value));
    stream_popChar(s);
    p += 1*Uri_getHexDigitValue(c.value);

    if(p != '%' && Uri_isPcharRaw(p, extra)) {
        sb_reset(&sb);
        sb_appendChar(&sb, p);
    }

    return just(MaybeString, sb_build(sb));
}

#define Uri_parsePchar(s, alloc, low) Uri_parsePcharRaw((s), (alloc), (low), mkString("@:"))

MaybeString Uri_parsePcharRawString(Stream *s, Alloc *alloc, bool lowercase, String extra) {
    StringBuilder sb = mkStringBuilderCap(64);
    sb.alloc = alloc;

    MaybeChar c = stream_peekChar(s);
    while(isJust(c) && Uri_isPcharRaw(c.value, extra)) {
        MaybeString pchar = Uri_parsePcharRaw(s, alloc, lowercase, extra);
        if(isFail(pchar, PCHAR_INVALID_PERCENT_ENCODING)) return fail(MaybeString, PCHAR_INVALID_PERCENT_ENCODING);
        if(isNone(pchar)) break; // eof or non pchar

        // a pchar can be either 1 or 3 characters (percent encoding)
        for(usz i = 0; i < pchar.value.len; i++) {
            sb_appendChar(&sb, pchar.value.s[i]);
        }
    }

    return just(MaybeString, sb_build(sb));
}

#define Uri_parsePcharString(s, alloc, low) Uri_parsePcharRawString((s), (alloc), (low), mkString("@:"))


#define SEGMENT_NO_COLON 1
#define SEGMENT_NON_ZERO 2
MaybeString Uri_parsePathSegment(Stream *s, Alloc *alloc, bool nonZero, bool noColon) {
    MaybeString str = noColon ? Uri_parsePcharRawString(s, alloc, false, mkString("@")) : Uri_parsePcharString(s, alloc, false);
    if(isNone(str)) return fail(MaybeString, str.errmsg);

    if(noColon) {
        MaybeChar c = stream_peekChar(s);
        if(isJust(c) && c.value == ':') return fail(MaybeString, SEGMENT_NO_COLON);
    }

    if(nonZero && str.value.len == 0) return fail(MaybeString, SEGMENT_NON_ZERO);

    return str;
}

bool Uri_parsePathInternal(UriPath *path, Stream *s, Alloc *alloc) {
    MaybeChar c;
    while(isJust(c = stream_peekChar(s)) && c.value == '/') {
        stream_popChar(s);

        MaybeString segmentMaybe = Uri_parsePathSegment(s, alloc, false, false);
        if(isFail(segmentMaybe, PCHAR_INVALID_PERCENT_ENCODING)) {
            *path = fail(UriPath, mkString("Invalid percent encoding" DEBUG_LOC));
            return false;
        }

        // NOTE: I kinda forgot how I handled errors here, may be unnecessary/bad
        if(isNone(segmentMaybe)) {
            *path = fail(UriPath, mkString("Something bad happened" DEBUG_LOC));
            return false;
        }

        dynar_append(&path->segments, String, segmentMaybe.value, _);
    }

    return true;
}

UriPath Uri_parsePathAbempty(Stream *s, Alloc *alloc) {
    UriPath path = {0};
    path.segments = mkDynarCA(String, 8, alloc);
    if(!Uri_parsePathInternal(&path, s, alloc)) return path;
    return path;
}

UriPath Uri_parsePathRootlessOrNoscheme(Stream *s, Alloc *alloc, bool noColon) {
    MaybeString segmentMaybe = Uri_parsePathSegment(s, alloc, true, noColon);
    if(isNone(segmentMaybe)) return fail(UriPath, mkString("The first segment of a rootless path cannot be empty" DEBUG_LOC));

    UriPath path = {0};
    path.segments = mkDynarCA(String, 8, alloc);
    dynar_append(&path.segments, String, segmentMaybe.value, _);

    if(!Uri_parsePathInternal(&path, s, alloc)) return path;
    return path;
}

UriPath Uri_parsePathRootless(Stream *s, Alloc *alloc) {
    UriPath path = Uri_parsePathRootlessOrNoscheme(s, alloc, false);
    path.type = URI_PATH_ROOTLESS;
    return path;
}

UriPath Uri_parsePathRootlessOrEmpty(Stream *s, Alloc *alloc) {
    MaybeChar c = stream_peekChar(s);
    if(isNone(c) || (isJust(c) && !Uri_isPchar(c.value))) {
        UriPath path = {0};
        path.type = URI_HIER_ABSOLUTE;
        path.segments = mkDynarCA(String, 1, alloc);
        dynar_append(&path.segments, String, mkString(""), _);
        return path;
    }
    
    return Uri_parsePathRootless(s, alloc);
}

UriPath Uri_parsePathNoscheme(Stream *s, Alloc *alloc) {
    UriPath path = Uri_parsePathRootlessOrNoscheme(s, alloc, true);
    path.type = URI_PATH_NOSCHEME;
    return path;
}

UriHost Uri_parseHostIpLiteral(Stream *s, Alloc *alloc) {
    s = s;
    alloc = alloc;
    // TODO: implement
    return fail(UriHost, mkString("Non IPv4 IP literal parsing is not implemented yet" DEBUG_LOC));
}

UriHost Uri_parseHostIpv4(Stream *s) {
    u8 values[4] = {0};
    MaybeChar c;
    for(int i = 0; i < 4; i++) {
        u8 acc = 0;
        while(isJust(c = stream_peekChar(s)) && Uri_isDigit(c.value)) {
            if(acc >= u8decmax) return fail(UriHost, mkString("Each number in IPv4 literal must fall in range 0..=255" DEBUG_LOC));
            acc *= 10;
            acc += c.value - '0';
            stream_popChar(s);
        }

        if(i != 3) {
            if(isNone(c) || (isJust(c) && c.value != '.')) return fail(UriHost, mkString("IPv4 format requires periods '.' between each number" DEBUG_LOC));
            stream_popChar(s);
        }

        values[i] = acc;
    }

    UriHost host = { .type = URI_HOST_IPV4, .ipv4 = { .a = values[0], .b = values[1], .c = values[2], .d = values[3] } };
    return host;
}

UriHost Uri_parseHost(Stream *s, Alloc *alloc) {
    MaybeChar c = stream_peekChar(s);
    if(isJust(c) && c.value == '[') return Uri_parseHostIpLiteral(s, alloc);

    StringBuilder buffer = mkStringBuilderCap(64);
    buffer.alloc = alloc;

    while(isJust(c = stream_peekChar(s)) && c.value != ':' && c.value != '/' && c.value != '?' && c.value != '#') {
        stream_popChar(s);
        sb_appendChar(&buffer, c.value);
    }

    Stream ipv4Stream = mkStreamStr(sb_build(buffer));
    UriHost ipv4Host = Uri_parseHostIpv4(&ipv4Stream);
    if(isJust(ipv4Host)) return ipv4Host;

    Stream regNameStream = mkStreamStr(sb_build(buffer));
    MaybeString regName = Uri_parsePcharRawString(&regNameStream, alloc, true, mkString(""));
    if(isFail(regName, PCHAR_INVALID_PERCENT_ENCODING)) return fail(UriHost, mkString("Invalid percent encoding" DEBUG_LOC));
    if(isNone(regName)) return fail(UriHost, mkString("This shouldn't happen" DEBUG_LOC));

    UriHost host = { .type = URI_HOST_REGNAME, .regName = regName.value };
    return host;
}

UriAuthority Uri_parseAuthorityWithoutUserinfo(Stream *s, Alloc *alloc) {
    UriAuthority authority = {0};

    UriHost host = Uri_parseHost(s, alloc);
    if(isNone(host)) return fail(UriAuthority, host.errmsg);
    authority.host = host;

    MaybeChar c = stream_peekChar(s);
    if(isNone(c)) return authority;
    if(isJust(c) && c.value != ':') return authority;
    stream_popChar(s);

    // TODO: parse port

    authority.hasPort = true;
    StringBuilder portsb = mkStringBuilderCap(16);
    bool portOverflow = false;
    u64 port = 0;

    while(isJust(c = stream_peekChar(s)) && Uri_isDigit(c.value)) {
        stream_popChar(s);

        if(port >= u64decmax) {
            portOverflow = true;
        }
        else {
            port *= 10;
            port += c.value - '0';
        }

        sb_appendChar(&portsb, c.value);
    }

    if(!portOverflow) authority.port = port;
    authority.portOverflow = portOverflow;
    authority.portString = sb_build(portsb);

    return authority;
}

UriAuthority Uri_parseAuthority(Stream *s, Alloc *alloc) {
    // NOTE: The URI spec is so good, that it seems to be impossible
    // to unambiguously detect the userinfo component, delimited by '@'.
    // So this is how I'll do it - read the whole authority (delimited
    // by either '/', '?', '#', or EOF), and check if that contains '@',
    // and act accordingly.
    // It seems to be possible to improve on that - if we detect a '@' 
    // before the whole authority is terminated, we get rid of the
    // ambiguity entirely and can continue parsing without additional
    // allocations

    UriAuthority authority = {0};
    authority.hasUserInfo = false;
    authority.hasPort = false;

    // StringBuilder userInfo = mkStringBuilderCap(64);
    // userInfo.alloc = alloc;

    StringBuilder buffer = mkStringBuilderCap(64);

    MaybeChar c;
    while(isJust(c = stream_peekChar(s)) && c.value != '/' && c.value != '?' && c.value != '#' && c.value != '@') {
        sb_appendChar(&buffer, c.value);
        stream_popChar(s);
    }

    if(isJust(c) && c.value == '@') {
        stream_popChar(s);
        authority.hasUserInfo = true;

        Stream ps = mkStreamStr(sb_build(buffer));
        MaybeString userInfo = Uri_parsePcharRawString(&ps, alloc, false, mkString(":"));

        if(isNone(userInfo)) {
            return fail(UriAuthority, mkString("Invalid percent encoding" DEBUG_LOC));
        }

        authority.userInfo = userInfo.value;
    }
    else {
        // Userinfo has not been detected, so we replace the stream
        // with the buffer that we have accumulated
        String authorityWithoutUserinfo = sb_build(buffer);
        Stream newStream = mkStreamStr(authorityWithoutUserinfo);
        s = &newStream;
    }

    UriAuthority authorityAux = Uri_parseAuthorityWithoutUserinfo(s, alloc);
    if(isNone(authorityAux)) return authorityAux;

    authorityAux.hasUserInfo = authority.hasUserInfo;
    authorityAux.userInfo = authority.userInfo;

    return authorityAux;
}

UriHierarchyPart Uri_parseHier(Stream *s, Alloc *alloc) {
    UriHierarchyPart hier = {0};
    hier.hasPath = true;
    MaybeChar c = stream_peekChar(s);
    if(isJust(c) && c.value == '/') {
        // NOTE: because we pop the '/' even if the path is absolute, we will be parsing the path as if it were a rootless path (or an empty one)
        stream_popChar(s);
        c = stream_peekChar(s);
        if(isJust(c) && c.value == '/') {
            // parse authority
            stream_popChar(s);
            hier.type = URI_HIER_AUTHORITY;
            hier.hasAuthority = true;

            UriAuthority authority = Uri_parseAuthority(s, alloc);
            if(isNone(authority)) return fail(UriHierarchyPart, authority.errmsg);

            UriPath path = Uri_parsePathAbempty(s, alloc);
            if(isNone(path)) return fail(UriHierarchyPart, path.errmsg);

            hier.authority = authority;
            hier.path = path;
            return hier;
        }
        else {
            // parse absolute (rootless or empty, because we popped the first slash)
            hier.type = URI_HIER_ABSOLUTE;
            UriPath absolutePath = Uri_parsePathRootlessOrEmpty(s, alloc);
            if(isNone(absolutePath)) return fail(UriHierarchyPart, absolutePath.errmsg);

            absolutePath.type = URI_PATH_ABSOLUTE;
            hier.path = absolutePath;
            return hier;
        }
    }
    else if(isJust(c) && Uri_isPchar(c.value)) {
        // parse rootless
        hier.type = URI_HIER_ROOTLESS;

        UriPath rootlessPath = Uri_parsePathRootless(s, alloc);
        if(isNone(rootlessPath)) return fail(UriHierarchyPart, rootlessPath.errmsg);

        hier.path = rootlessPath;
        return hier;
    }
    else {
        // empty path
        hier.type = URI_HIER_EMPTY;
        hier.hasPath = false;
        return hier;
    }

    return fail(UriHierarchyPart, mkString("Unreachable" DEBUG_LOC));
}

MaybeString Uri_parseQuery(Stream *s, Alloc *alloc) {
    return Uri_parsePcharRawString(s, alloc, false, mkString("@:/?"));
}

MaybeString Uri_parseFragment(Stream *s, Alloc *alloc) {
    return Uri_parsePcharRawString(s, alloc, false, mkString("@:/?"));
}

Uri Uri_parseUri(Stream *s, Alloc *alloc) {
    Uri uri = {0};

    MaybeString scheme = Uri_parseScheme(s, alloc);
    if(isNone(scheme)) return fail(Uri, *(String *)scheme.errmsg);
    Uri_lowercase(&scheme.value);
    uri.scheme = scheme.value;

    MaybeChar c = stream_peekChar(s);
    if(isNone(c)) return fail(Uri, mkString("Failed to read a character" DEBUG_LOC));
    if(isJust(c) && c.value != ':') return fail(Uri, mkString("A colon ':' is missing after the scheme" DEBUG_LOC));
    stream_popChar(s);

    UriHierarchyPart hier = Uri_parseHier(s, alloc);
    if(isNone(hier)) return fail(Uri, hier.errmsg);
    uri.hierarchyPart = hier;

    c = stream_peekChar(s);
    if(isJust(c) && c.value == '?') {
        stream_popChar(s);
        MaybeString query = Uri_parseQuery(s, alloc);
        if(isFail(query, PCHAR_INVALID_PERCENT_ENCODING)) return fail(Uri, mkString("Invalid percent encoding" DEBUG_LOC));
        if(isNone(query)) return fail(Uri, mkString("This shouldn't happen" DEBUG_LOC));
        uri.query = query.value;
    }

    c = stream_peekChar(s);
    if(isJust(c) && c.value == '#') {
        stream_popChar(s);
        MaybeString fragment = Uri_parseFragment(s, alloc);
        if(isFail(fragment, PCHAR_INVALID_PERCENT_ENCODING)) return fail(Uri, mkString("Invalid percent encoding" DEBUG_LOC));
        if(isNone(fragment)) return fail(Uri, mkString("This shouldn't happen" DEBUG_LOC));
        uri.fragment = fragment.value;
    }

    c = stream_peekChar(s);
    if(!isNone(c)) {
        printf("UNEXPECTED CHARACTER: %x\n", c.value);
        return fail(Uri, mkString("Unexpected character at the end of the URI" DEBUG_LOC));
    }

    return uri;
}

UriPath Uri_pathMoveRelatively(UriPath base, UriPath move, Alloc *alloc) {
    UriPath result = {0};
    result.segments = mkDynarCA(String, base.segments.len, alloc);

    for(usz i = 0; i < base.segments.len; i++) {
        String segment = dynar_index(String, &base.segments, i);
        // we need to clone, cuz base might be using a different allocator
        dynar_append_clone(&result.segments, segment);
    }

    for(usz i = 0; i < move.segments.len; i++) {
        String segment = dynar_index(String, &move.segments, i);
        if(mem_eq(segment, mkString("."))) {
            continue;
        }
        else if(mem_eq(segment, mkString(".."))) {
            if(result.segments.len == 0) continue;
            dynar_pop(String, &result.segments);
        }
        else {
            dynar_append_clone(&result.segments, segment);
        }
    }

    return result;
}

bool Uri_pathHasPrefix(UriPath prefix, UriPath path) {
    if(prefix.segments.len > path.segments.len) return false;

    for(usz i = 0; i < prefix.segments.len; i++) {
        String ls = dynar_index(String, &prefix.segments, i);
        String rs = dynar_index(String, &path.segments, i);
        if(!mem_eq(ls, rs)) return false;
    }

    return true;
}

#endif // __LIB_URI
