#ifndef __LIB_STR
#define __LIB_STR

// TODO: remove this
#include <string.h>

#include "runes.h"
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
    for(usz i = 0; i < len; i++) {
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
    String s;
    usz cap;

    bool dontExpand;

    Alloc *alloc;
} StringBuilder;

#define mkStringBuilder() mkStringBuilderCap(32)
#define mkStringBuilderCap(c) ((StringBuilder){ .alloc = &ALLOC, .cap = (c) })

#define sb_build(sb) ((String){ .s = (sb).s.s, .len = (sb).s.len })

bool sb_appendMem(StringBuilder *sb, Mem m) {
    if(sb->s.s == null) {
        sb->s.s = AllocateBytesC(sb->alloc, sb->cap);
        sb->s.len = 0;
    }

    if(m.len + sb->s.len <= sb->cap) {
        Mem dst = mkMem(sb->s.s, sb->cap);
        dst = memIndex(dst, sb->s.len);
        mem_copy(dst, m);
        sb->s.len += m.len;
        return true;
    }

    usz oldLen = sb->s.len;
    usz newCap = sb->cap * 2;
    if(newCap < sb->s.len + m.len) newCap = sb->s.len + m.len;

    byte *newBytes = AllocateBytesC(sb->alloc, newCap);
    if(!newBytes) return false;

    Mem newS = mkMem(newBytes, newCap);
    mem_copy(newS, sb->s);
    FreeC(sb->alloc, sb->s.s);
    sb->cap = newCap;
    sb->s = newS;
    sb->s.len = oldLen;

    return sb_appendMem(sb, m);
}
#define sb_appendString(sb, m) sb_appendMem(sb, m)

bool sb_appendByte(StringBuilder *sb, byte b) {
    Mem m = mkMem(&b, 1);
    return sb_appendMem(sb, m);
}
#define sb_appendChar(sb, b) sb_appendByte(sb, b)

void sb_reset(StringBuilder *sb) {
    for(usz i = 0; i < sb->cap; i++) {
        sb->s.s[i] = 0x00;
    }
    sb->s.len = 0;
}

bool sb_appendRune(StringBuilder *sb, rune r) {
    i8 len = getRuneLen(r);
    Mem m = mkMem((byte *)&r, len);
    return sb_appendMem(sb, m);
}

bool string_contains(byte c, String s) {
    for(usz i = 0; i < s.len; i++) {
        if(s.s[i] == c) return true;
    }
    return false;
}

#endif // __LIB_STR
