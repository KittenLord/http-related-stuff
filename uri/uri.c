#include <types.h>
#include <str.h>
#include <macros.h>

// NOTE: If I got it correctly, the RFC3986 only permits the character set that is used in US-ASCII, and everything else is to be encoded using percent-encodings, so this implementation uses char everywhere

typedef u8 UriHostType;
typedef struct {
    bool error;
    String errmsg;

    UriHostType type;
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
} UriAuthority;

typedef struct UriPathSegment UriPathSegment;
struct UriPathSegment {
    bool error;
    u8 errmsg;

    String segment;
    UriPathSegment *next;
};

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

    usz segmentCount;
    UriPathSegment *segments;
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
    c >= 'a' && c <= 'z' ||
    c >= 'A' && c <= 'Z' ;
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
    c >= 'a' && c <= 'z' ||
    c >= 'A' && c <= 'Z' ||
    c >= '0' && c <= '9' ||
    c == '-'             ||
    c == '.'             ||
    c == '_'             ||
    c == '~'             ;
}

bool Uri_isHexdigit(byte c) {
    return
    c >= '0' && c <= '9' ||
    c >= 'a' && c <= 'f' ||
    c >= 'A' && c <= 'F' ;
}

byte Uri_normalizePercentByte(byte c) {
    if(c >= 'a' && c <= 'f') return c + ('A' - 'a');
    return c;
}

bool Uri_isPchar(byte c) {
    return
    Uri_isUnreserved(c)            ||
    c == '%'                       ||
    Uri_isSubcomponentDelimiter(c) ||
    c == '@'                       ||
    c == ':'                       ;
}

MaybeString Uri_parseScheme(PeekStream *s, Alloc *alloc) {
    // TODO: figure out a good approach to this
    static String error_failedToReadChar = mkString("Failed to read a character" DEBUG_LOC);
    static String error_firstCharAlpha = mkString("First character of the scheme must be a letter" DEBUG_LOC);

    StringBuilder sb = mkStringBuilderCap(16);
    sb.alloc = alloc;

    MaybeChar c = pstream_peekChar(s);

    if(isNone(c)) return fail(MaybeString, (u64)&error_failedToReadChar);
    if(isJust(c) && !Uri_isAlpha(c.value)) return fail(MaybeString, (u64)&error_firstCharAlpha);

    sb_appendChar(&sb, c.value);
    pstream_popChar(s);

    while(isJust(c = pstream_peekChar(s)) && Uri_isSchemeChar(c.value)) {
        sb_appendChar(&sb, c.value);
        pstream_popChar(s);
    }

    return just(MaybeString, sb_build(sb));
}

#define PCHAR_INVALID_PERCENT_ENCODING 1
MaybeString Uri_parsePchar(PeekStream *s, Alloc *alloc) {
    MaybeChar c = pstream_peekChar(s);
    if(isNone(c)) return none(MaybeString);

    StringBuilder sb = mkStringBuilderCap(3);
    sb.alloc = alloc;
    sb_appendChar(&sb, c.value);

    if(Uri_isUnreserved(c.value)            ||
       Uri_isSubcomponentDelimiter(c.value) || 
       c.value == '@'                       ||
       c.value == ':'                       ) {

        pstream_popChar(s);
        return just(MaybeString, sb_build(sb));
    }

    if(c.value != '%') return none(MaybeString);

    pstream_popChar(s);

    c = pstream_peekChar(s);
    if(isNone(c) || (isJust(c) && !Uri_isHexdigit(c.value))) return fail(MaybeString, PCHAR_INVALID_PERCENT_ENCODING);
    sb_appendChar(&sb, Uri_normalizePercentByte(c.value));
    pstream_popChar(s);

    c = pstream_peekChar(s);
    if(isNone(c) || (isJust(c) && !Uri_isHexdigit(c.value))) return fail(MaybeString, PCHAR_INVALID_PERCENT_ENCODING);
    sb_appendChar(&sb, Uri_normalizePercentByte(c.value));
    pstream_popChar(s);

    return just(MaybeString, sb_build(sb));
}

#define SEGMENT_NO_COLON 1
#define SEGMENT_NON_ZERO 2
UriPathSegment Uri_parsePathSegment(PeekStream *s, Alloc *alloc, bool nonZero, bool noColon) {
    StringBuilder sb = mkStringBuilderCap(64);
    sb.alloc = alloc;

    MaybeChar c = pstream_peekChar(s);
    while(isJust(c) && Uri_isPchar(c.value)) {
        if(noColon && c.value == ':') return fail(UriPathSegment, SEGMENT_NO_COLON);

        MaybeString pchar = Uri_parsePchar(s, alloc);
        if(isFail(pchar, PCHAR_INVALID_PERCENT_ENCODING)) return fail(UriPathSegment, PCHAR_INVALID_PERCENT_ENCODING);
        if(isNone(pchar)) break;

        // a pchar can be either 1 or 3 characters (percent encoding)
        for(int i = 0; i < pchar.value.len; i++) {
            sb_appendChar(&sb, pchar.value.s[i]);
        }
    }

    if(nonZero && sb.len == 0) return fail(UriPathSegment, SEGMENT_NON_ZERO);

    UriPathSegment segment = { .segment = sb_build(sb) };
    return segment;
}

UriPath Uri_parsePathAbempty(PeekStream *s, Alloc *alloc) {
    UriPath path = {0};
    UriPathSegment *last = null;

    MaybeChar c;
    while(isJust(c = pstream_peekChar(s)) && c.value == '/') {
        pstream_popChar(s);

        AllocateVarC(UriPathSegment, segment, Uri_parsePathSegment(s, alloc, false, false), alloc);
        if(isNone(*segment)) return fail(UriPath, mkString("This shouldn't happen" DEBUG_LOC));

        path.segmentCount++;
        if(last == null) {
            path.segments = segment;
            last = segment;
        }
        else {
            last->next = segment;
            last = segment;
        }
    }

    return path;
}

UriPath Uri_parsePathRootlessOrNoscheme(PeekStream *s, Alloc *alloc, bool noColon) {
    AllocateVarC(UriPathSegment, initSegment, Uri_parsePathSegment(s, alloc, true, noColon), alloc);
    if(isNone(*initSegment)) return fail(UriPath, mkString("The first segment of a rootless path cannot be empty" DEBUG_LOC));

    UriPath rest = Uri_parsePathAbempty(s, alloc);
    if(isNone(rest)) return rest;

    initSegment->next = rest.segments;
    rest.segments = initSegment;
    rest.segmentCount++;
    return rest;
}

UriPath Uri_parsePathRootless(PeekStream *s, Alloc *alloc) {
    UriPath path = Uri_parsePathRootlessOrNoscheme(s, alloc, false);
    path.type = URI_PATH_ROOTLESS;
    return path;
}

UriPath Uri_parsePathNoscheme(PeekStream *s, Alloc *alloc) {
    UriPath path = Uri_parsePathRootlessOrNoscheme(s, alloc, true);
    path.type = URI_PATH_NOSCHEME;
    return path;
}

UriHost Uri_parseHost(PeekStream *s, Alloc *alloc) {

}

UriAuthority Uri_parseAuthority(PeekStream *s, Alloc *alloc) {
    // NOTE: The URI spec is so good, that it seems to be impossible
    // to unambiguously detect the userinfo component, delimited by '@'.
    // So this is how I'll do it - read the whole authority (delimited
    // by either '/', '?', '#', or EOF), and check if that contains '@',
    // and act accordingly.
    // It seems to be possible to improve on that - if we detect a '@' 
    // before the whole authority is terminated, we get rid of the
    // ambiguity entirely and can continue parsing without additional
    // allocations

    StringBuilder userinfo = mkStringBuilderCap(64);
    userinfo.alloc = alloc;

    StringBuilder buffer = mkStringBuilderCap(64);

    MaybeChar c;
    while(isJust(c = pstream_peekChar(s)) && c.value != '/' && c.value != '?' && c.value != '#' && c.value != '@') {
        sb_appendChar(&buffer, c.value);
        sb_appendChar(&userinfo, c.value);
    }

    if(isJust(c) && c.value == '@') {
        pstream_popChar(s);
    }
    else {
        // Userinfo has not been detected, so we replace the stream
        // with the buffer that we have accumulated
        String authorityWithoutUserinfo = sb_build(buffer);
        PeekStream newStream = mkPeekStream(mkStreamStr(authorityWithoutUserinfo));
        s = &newStream;
    }
}

UriHierarchyPart Uri_parseHier(PeekStream *s, Alloc *alloc) {
    UriHierarchyPart hier = {0};
    hier.hasPath = true;
    MaybeChar c = pstream_peekChar(s);
    if(isJust(c) && c.value == '/') {
        // NOTE: because we pop the '/' even if the path is absolute, we will be parsing the path as if it were a rootless path (or an empty one)
        pstream_popChar(s);
        c = pstream_peekChar(s);
        if(isJust(c) && c.value == '/') {
            // parse authority
            pstream_popChar(s);
            hier.type = URI_HIER_AUTHORITY;

            UriAuthority authority = Uri_parseAuthority(s, alloc);
        }
        else {
            // parse absolute (rootless or empty, because we popped the first slash)
            hier.type = URI_HIER_ABSOLUTE;

            if(isNone(c) || (isJust(c) && Uri_isPchar(c.value))) {
                UriPath path = {0};
                path.type = URI_HIER_ABSOLUTE;
                path.segmentCount = 1;
                AllocateVarC(UriPathSegment, emptySegment, (UriPathSegment){ .segment = mkString("") }, alloc);
                path.segments = emptySegment;
                hier.path = path;

                return hier;
            }

            UriPath absolutePath = Uri_parsePathRootless(s, alloc);
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
}

Uri Uri_parseUri(PeekStream *s, Alloc *alloc) {
    Uri uri = {0};

    MaybeString scheme = Uri_parseScheme(s, alloc);
    if(isNone(scheme)) return fail(Uri, *(String *)scheme.errmsg);
    uri.scheme = scheme.value;

    MaybeChar c = pstream_peekChar(s);
    if(isNone(c)) return fail(Uri, mkString("Failed to read a character" DEBUG_LOC));
    if(isJust(c) && c.value != ':') return fail(Uri, mkString("A colon ':' is missing after the scheme" DEBUG_LOC));
    pstream_popChar(s);

    UriHierarchyPart hier = Uri_parseHier(s, alloc);
    if(isNone(hier)) return fail(Uri, hier.errmsg);
    uri.hierarchyPart = hier;
}
