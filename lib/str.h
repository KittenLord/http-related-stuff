#ifndef __LIB_STR
#define __LIB_STR

// TODO: remove this
#include <string.h>

#include "alloc.h"
#include "types.h"
#include "mem.h"

typedef Mem String;

#define mkStringLen(_s, _len) ((String){ .s = (byte *)(_s), .len = (_len) })
#define mkString(_s) ((String){ .s = (byte *)(_s), .len = strlen((_s)) })

bool str_equal(String a, String b) {
    if(a.s == null || b.s == null) return false;
    if(a.len != b.len) return false;
    usz len = a.len;

    // NOTE: I'm pretty sure this will still work for utf-8 strings
    for(int i = 0; i < len; i++) {
        if(a.s[i] != b.s[i]) return false;
        return true;
    }

    return true;
}

#define STRING_NONE 0
typedef struct {
    String value;
    u64 errmsg;
    bool error;
} MaybeString;

typedef struct {
    byte *s;
    usz len;
    usz cap;

    bool dontExpand;

    Alloc *alloc;
} StringBuilder;

#define mkStringBuilder() ((StringBuilder){ .alloc = &ALLOC })
#define mkStringBuilderCap(c) ((StringBuilder){ .alloc = &ALLOC, .cap = (c) })

#define sb_build(sb) ((String){ .s = (sb).s, .len = (sb).len })

bool sb_appendChar(StringBuilder *sb, byte c) {
    if(sb->s && sb->len < sb->cap) { sb->s[sb->len++] = c; return true; }
    if(sb->dontExpand) return false;
    if(sb->cap == 0) sb->cap = 64;
    usz newCap = sb->s == null ? sb->cap : sb->cap * 2;
    byte *new = AllocateBytesC(sb->alloc, newCap);
    if(sb->s) memcpy(new, sb->s, sb->cap);
    FreeC(sb->alloc, sb->s);
    sb->s = new;
    sb->cap = newCap;
    sb->s[sb->len++] = c;
    return true;
}

void sb_reset(StringBuilder *sb) {
    if(!sb) return;
    if(!sb->s) return;
    sb->len = 0;
    for(int i = 0; i < sb->cap; i++) {
        sb->s[i] = 0x00;
    }
}

bool sb_appendString(StringBuilder *sb, String str) {
    bool result = true;
    for(int i = 0; i < str.len; i++) {
        result = result && sb_appendChar(sb, str.s[i]);
    }
    return result;
}

bool sb_appendRune(StringBuilder *sb, rune r) {
    // NOTE: this will only work on little endian lmao
    byte *data = (byte *)&r;
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

bool string_contains(byte c, String s) {
    for(int i = 0; i < s.len; i++) {
        if(s.s[i] == c) return true;
    }
    return false;
}

#endif // __LIB_STR
