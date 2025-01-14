#ifndef __LIB_RUNES
#define __LIB_RUNES

#include "types.h"
#include "macros.h"

#define RUNE_NONE 0
#define RUNE_EOF 0
#define RUNE_INVALID 1
#define RUNE_UNFINISHED 2
typedef struct {
    rune value;
    bool error;
    u8 errmsg;
} MaybeRune;

#define rune1(c0) (c0) 
#define rune2(c0, c1) ((c1) << 8 | (c0))
#define rune3(c0, c1, c2) ((c2) << 16 | (c1) << 8 | (c0))
#define rune4(c0, c1, c2, c3) ((c3) << 24 | (c2) << 16 | (c1) << 8 | (c0))

bool runeUtfTail(byte c) {
    return c >= 0x80 && c <= 0xBF;
}

bool runeUtf1(byte c0) {
    return
    (c0 >= 0x00 && c0 <= 0x7F);
}

bool runeUtf2(byte c0, byte c1, u8 cap) {
    return
    (c0 >= 0xC2 && c0 <= 0xDF) && (runeUtfTail(c1) || cap <= 1);
}

bool runeUtf3(byte c0, byte c1, byte c2, u8 cap) {
    return
    (c0 == 0xE0) && ((c1 >= 0xA0 && c1 <= 0xBF) || cap <= 1) && (runeUtfTail(c2) || cap <= 2) ||
    (c0 >= 0xE1 && c0 <= 0xEC) && (runeUtfTail(c1) || cap <= 1) && (runeUtfTail(c2) || cap <= 2) ||
    (c0 == 0xED) && ((c1 >= 0x80 && c1 <= 0x9F) || cap <= 1) && (runeUtfTail(c2) || cap <= 2) ||
    (c0 >= 0xEE && c0 <= 0xEF) && (runeUtfTail(c1) || cap <= 1) && (runeUtfTail(c2) || cap <= 2);
}

bool runeUtf4(byte c0, byte c1, byte c2, byte c3, u8 cap) {
    return
    (c0 == 0xF0) && ((c1 >= 0x90 && c1 <= 0xBF) || cap <= 1) && (runeUtfTail(c2) || cap <= 2) && (runeUtfTail(c3) || cap <= 3) ||
    (c0 >= 0xF1 && c0 <= 0xF3) && (runeUtfTail(c1) || cap <= 1) && (runeUtfTail(c2) || cap <= 2) && (runeUtfTail(c3) || cap <= 3) ||
    (c0 == 0xF4) && ((c1 >= 0x80 && c1 <= 0x8F) || cap <= 1) && (runeUtfTail(c2) || cap <= 2) && (runeUtfTail(c3) || cap <= 3);
}

i8 getRuneLen(rune r) {
    if(r == (r & 0x00000000)) return 1;
    if(r == (r & 0x000000FF)) return 1;
    if(r == (r & 0x0000FFFF)) return 2;
    if(r == (r & 0x00FFFFFF)) return 3;
    if(r == (r & 0xFFFFFFFF)) return 4;
    return -1;
}

MaybeRune getRune(byte *data, usz len) {
    if(len == 1) {
        byte c0 = *data;
        if(runeUtf1(c0)) return just(MaybeRune, rune1(c0));
        else if(runeUtf2(c0, 0, 1) ||
                runeUtf3(c0, 0, 0, 1) ||
                runeUtf4(c0, 0, 0, 0, 1)) return fail(MaybeRune, RUNE_UNFINISHED);
        return fail(MaybeRune, RUNE_INVALID);
    }
    else if(len == 2) {
        byte c0 = *data;
        byte c1 = *(data + 1);
        if(runeUtf2(c0, c1, 2)) return just(MaybeRune, rune2(c0, c1));
        else if(runeUtf3(c0, c1, 0, 2) ||
                runeUtf4(c0, c1, 0, 0, 2)) return fail(MaybeRune, RUNE_UNFINISHED);
        return fail(MaybeRune, RUNE_INVALID);
    }
    else if(len == 3) {
        byte c0 = *data;
        byte c1 = *(data + 1);
        byte c2 = *(data + 2);
        if(runeUtf3(c0, c1, c2, 3)) return just(MaybeRune, rune3(c0, c1, c2));
        else if(runeUtf4(c0, c1, c2, 0, 3)) return fail(MaybeRune, RUNE_UNFINISHED);
        return fail(MaybeRune, RUNE_INVALID);
    }
    else if(len == 4) {
        byte c0 = *data;
        byte c1 = *(data + 1);
        byte c2 = *(data + 2);
        byte c3 = *(data + 3);
        if(runeUtf4(c0, c1, c2, c3, 4)) return just(MaybeRune, rune4(c0, c1, c2, c3));
        return fail(MaybeRune, RUNE_INVALID);
    }

    return fail(MaybeRune, RUNE_INVALID);
}

#endif // __LIB_RUNES
