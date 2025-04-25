#ifndef __LIB_DYNAR
#define __LIB_DYNAR

typedef struct {
    Mem mem;
    usz len;
    usz element;
    Alloc *alloc;
} _dynar;
// Type safety in C be like
#define Dynar(ty) _dynar

#define DYNAR_DEFAULT_CAPACITY 32

Dynar(void) makeDynarFixed(usz element, Mem mem, usz len) {
    mem.len -= mem.len % element;
    return (Dynar(void)){
        .mem = mem,
        .len = len,
        .element = element,
        .alloc = null
    };
}

Dynar(void) makeDynarAllocate(usz element, usz capacity, Alloc *alloc) {
    usz len = element * capacity;
    byte *bytes = AllocateBytesC(alloc, len);
    Mem mem = mkMem(bytes, len);
    return (Dynar(void)){
        .mem = mem,
        .len = 0,
        .element = element,
        .alloc = alloc
    };
}

#define mkDynarML(ty, mem, len) makeDynarFixed(sizeof(ty), mem, len)
#define mkDynarM(ty, mem) mkDynarM(ty, mem, 0)
#define mkDynarCA(ty, cap, alloc) makeDynarAllocate(sizeof(ty), (cap), (alloc))
#define mkDynarA(ty, alloc) mkDynarCA(ty, DYNAR_DEFAULT_CAPACITY, alloc)
#define mkDynar(ty) mkDynarCA(ty, DYNAR_DEFAULT_CAPACITY, ALLOC)

#define dynar_append(dynar, ty, _value, result) { \
    ty value = _value; \
    bool _ = true; \
    if(dynar->len * dynar->element < dynar->mem.len) { \
        *((ty *)dynar->mem.s + dynar->len) = value; \
        dynar->len++; \
    } \
    else if(dynar->alloc == null){ \
        result = false; \
    } \
    else { \
        usz newCap = dynar->mem.s * 2; \
        if(newCap == 0) newCap = DYNAR_DEFAULT_CAPACITY * dynar->element; \
        byte *bytes = AllocateBytesC(alloc, newCap); \
        dynar->mem.s = bytes; \
        dynar->mem.len = newCap; \
        *((ty *)dynar->mem.s + dynar->len) = value; \
        dynar->len++; \
    } \
} \

#endif // __LIB_DYNAR
