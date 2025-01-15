#include <types.h>
#include <str.h>

typedef struct {

} UriHierarchyPart;

typedef struct {
    String scheme;
    UriHierarchyPart hierarchyPart;

    bool hasQuery;
    String query;

    bool hasFragment;
    String fragment;
} Uri;

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

bool Uri_isHexdigit(byte c) {
    return
    c >= '0' && c <= '9' ||
    c >= 'a' && c <= 'f' ||
    c >= 'A' && c <= 'F' ;
}
