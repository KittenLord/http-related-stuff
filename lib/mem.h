#ifndef __LIB_MEM
#define __LIB_MEM

#include "types.h"
#include "alloc.h"

typedef struct {
    byte *s;
    usz len;
} Mem;

#define memnull ((Mem){ .s = null, .len = 0 })
#define mkMem(_s, _len) ((Mem){ .s = (_s), .len = (_len) })
#define mkPointer(_v) mkMem((byte *)&_v, sizeof(void *))

#define memIndex(m, i) mkMem((m).s + i, (m).len - i)
#define memLimit(m, i) mkMem((m).s, (i) > (m).len ? (m).len : (i))

bool mem_eq(Mem a, Mem b) {
    if(a.s == null && b.s == null) return true;
    if(a.s == null || b.s == null) return false;
    if(a.len != b.len) return false;
    for(usz i = 0; i < a.len; i++) {
        if(a.s[i] != b.s[i]) return false;
    }
    return true;
}

void mem_set(Mem a, byte v) {
    // if(a.s == null) return;
    for(usz i = 0; i < a.len; i++) {
        a.s[i] = v;
    }
}

void mem_copy(Mem dst, Mem src) {
    // if(dst.s == null || src.s == null) return;
    for(usz i = 0; i < dst.len && i < src.len; i++) {
        dst.s[i] = src.s[i];
    }
}

void mem_move(Mem dst, Mem src) {
    // if(dst.s == null || src.s == null) return;
    // TODO: I'm not completely sure how strict aliasing works
    // in context of comparing two pointers, so I'll leave this
    // for later
    memmove(dst.s, src.s, dst.len < src.len ? dst.len : src.len);
}

Mem mem_clone(Mem src, Alloc *alloc) {
    byte *s = AllocateBytesC(alloc, src.len);
    Mem dst = { .s = s, .len = src.len };
    mem_copy(dst, src);
    return dst;
}

#endif // __LIB_MEM
