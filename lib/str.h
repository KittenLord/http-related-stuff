#ifndef __LIB_STR
#define __LIB_STR

// TODO: remove this
#include <string.h>

#include "alloc.h"
#include "types.h"

typedef struct {
    byte *s;
    usz len;
} String;

typedef struct {
    byte *s;
    usz len;
    usz cap;

    bool dontExpand;

    Alloc *alloc;
} StringBuilder;

#define mkStringBuilder() ((StringBuilder){ .alloc = &ALLOC })
#define sb_build(sb) ((String){ .s = (sb).s, .len = (sb).len })

bool sb_appendChar(StringBuilder *sb, char c) {
    if(sb->len < sb->cap) { sb->s[sb->len++] = c; return true; }
    if(sb->dontExpand) return false;
    if(sb->cap == 0) sb->cap = 64;
    usz newCap = sb->s == null ? sb->cap : sb->cap * 2;
    byte *new = sb->alloc->alloc(*sb->alloc, newCap);
    if(sb->s) memcpy(new, sb->s, sb->cap);
    sb->s = new;
    sb->cap = newCap;
    sb->s[sb->len++] = c;
    return true;
}

bool sb_appendRune(StringBuilder *sb, rune r) {
    // NOTE: this will only work on little endian lmao
    char *data = (char *)&r;
    int i = 0;
    if(data[0] == '\0') { return sb_appendChar(sb, '\0'); }
    bool result;
    while(i < 4 && data[i] != '\0') { 
        result = sb_appendChar(sb, data[i]); 
        i++;

        if(!result) break;
    }

    if(result) return true;

    for(; i > 0; i--) {
        sb->len--;
    }

    return false;
}

#define mkStringLen(_s, _len) ((String){ .s = _s, .len = _len })
#define mkString(_s) ((String){ .s = _s, .len = strlen(_s) })

#endif // __LIB_STR
