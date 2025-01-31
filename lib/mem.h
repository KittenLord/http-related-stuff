#ifndef __LIB_MEM
#define __LIB_MEM

#include "types.h"
#include "alloc.h"

typedef struct {
    byte *s;
    usz len;
} Mem;

bool mem_eq(Mem a, Mem b) {
    if(a.s == null && b.s == null) return true;
    if(a.s == null || b.s == null) return false;
    if(a.len != b.len) return false;
    for(int i = 0; i < a.len; i++) {
        if(a.s[i] != b.s[i]) return false;
    }
    return true;
}

void mem_set(Mem a, byte v) {
    if(a.s == null) return;
    for(int i = 0; i < a.len; i++) {
        a.s[i] = v;
    }
}

void mem_copy(Mem dst, Mem src) {
    if(dst.s == null || src.s == null) return;
    for(int i = 0; i < dst.len && i < src.len; i++) {
        dst.s[i] = src.s[i];
    }
}

Mem mem_clone(Mem src, Alloc *alloc) {
    byte *s = AllocateBytesC(alloc, src.len);
    Mem dst = { .s = s, .len = src.len };
    mem_copy(dst, src);
    return dst;
}

#endif // __LIB_MEM
