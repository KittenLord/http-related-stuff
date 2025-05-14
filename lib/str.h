#ifndef __LIB_STR
#define __LIB_STR

// TODO: remove this
#include <string.h>

#include "runes.h"
#include "alloc.h"
#include "types.h"
#include "mem.h"

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

    usz len;
    usz cap;

    bool dontExpand;

    Alloc *alloc;
} StringBuilder;

#define mkStringBuilder() mkStringBuilderCap(32)
#define mkStringBuilderCap(c) ((StringBuilder){ .alloc = ALLOC, .cap = (c) })
#define mkStringBuilderMem(m) ((StringBuilder){ .alloc = null, .s = (m), .len = 0, .cap = (m).len, .dontExpand = true })

#define sb_build(sb) ((String){ .s = (sb).s.s, .len = (sb).len })

bool sb_appendMem(StringBuilder *sb, Mem m) {
    if(sb->s.s == null) {
        sb->s = AllocateBytesC(sb->alloc, sb->cap);
    }

    if(m.len + sb->len <= sb->cap) {
        Mem dst = memIndex(sb->s, sb->len);
        mem_copy(dst, m);
        sb->len += m.len;
        return true;
    }

    if(sb->dontExpand) return false;

    usz newCap = sb->cap * 2;
    if(newCap < sb->len + m.len) newCap = sb->len + m.len;

    Mem newMem = AllocateBytesC(sb->alloc, newCap);
    if(isNull(newMem)) return false;

    mem_copy(newMem, sb->s);
    FreeC(sb->alloc, sb->s.s);
    sb->cap = newCap;
    sb->s = newMem;

    return sb_appendMem(sb, m);
}
#define sb_appendString(sb, m) sb_appendMem(sb, m)

bool sb_appendByte(StringBuilder *sb, byte b) {
    Mem m = mkMem(&b, 1);
    return sb_appendMem(sb, m);
}
#define sb_appendChar(sb, b) sb_appendByte(sb, b)

void sb_reset(StringBuilder *sb) {
    if(sb->s.s == null) return;
    mem_set(sb->s, 0x00);
    sb->len = 0;
}

bool sb_appendRune(StringBuilder *sb, rune r) {
    i8 len = getRuneLen(r);
    Mem m = mkMem((byte *)&r, len);
    return sb_appendMem(sb, m);
}

// TODO: rename to string_contains_any?
bool string_contains(byte c, String s) {
    for(usz i = 0; i < s.len; i++) {
        if(s.s[i] == c) return true;
    }
    return false;
}

void string_reverse(String s) {
    if(s.len == 0) return;
    usz lhs = 0;
    usz rhs = s.len - 1;
    while(lhs < rhs) {
        byte temp = s.s[lhs];
        s.s[lhs] = s.s[rhs];
        s.s[rhs] = temp;

        lhs += 1;
        rhs -= 1;
    }
}

#endif // __LIB_STR
