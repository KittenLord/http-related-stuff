#ifndef __LIB_DYNAR
#define __LIB_DYNAR

typedef struct {
    Mem mem;
    usz len;
    usz element;
    Alloc *alloc;
} _dynar_type;
// Type safety in C be like
#define Dynar(ty) _dynar_type

#define dynar_index(ty, dynar, index) (((ty *)((dynar)->mem.s))[(index)])

#define dynar_peek(ty, dynar) dynar_index(ty, dynar, (dynar)->len - 1)

#define dynar_set(ty, dynar, index, value) (dynar_index(ty, dynar, index) = (value))

#define dynar_isInit(dynar) ((dynar)->mem.s != null)

#define dynar_remove(ty, dynar, index) do { \
    if((dynar)->len == 1) { (dynar)->len = 0; break; } \
    byte *dst = (void *)(((ty *)((dynar)->mem.s)) + (index)); \
    byte *src = (void *)(((ty *)((dynar)->mem.s)) + (index) + 1); \
    usz len = ((dynar)->len - (index) - 1) * (dynar)->element; \
    mem_move(mkMem(dst, len), mkMem(src, len)); \
    (dynar)->len -= 1; \
} while(false)

#define dynar_pop(ty, dynar) dynar_remove(ty, dynar, (dynar)->len - 1)

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

void dynar_init(Dynar(void) *dynar) {
    if(dynar_isInit(dynar)) return;
    dynar->mem = AllocateBytesC(dynar->alloc, dynar->mem.len);
}

Dynar(void) makeDynarAllocate(usz element, usz capacity, Alloc *alloc, bool init) {
    usz len = element * capacity;
    Dynar(void) dynar = {
        .mem = mkMem(null, len),
        .len = 0,
        .element = element,
        .alloc = alloc
    };
    if(init) dynar_init(&dynar);
    return dynar;
}

#define mkDynarML(ty, mem, len) makeDynarFixed(sizeof(ty), mem, len)
#define mkDynarM(ty, mem) mkDynarML(ty, mem, 0)
#define mkDynarCAI(ty, cap, alloc, init) makeDynarAllocate(sizeof(ty), (cap), (alloc), init)
#define mkDynarCA(ty, cap, alloc) mkDynarCAI(ty, cap, alloc, false)
#define mkDynarAI(ty, alloc, init) mkDynarCAI(ty, DYNAR_DEFAULT_CAPACITY, alloc, init)
#define mkDynarA(ty, alloc) mkDynarCA(ty, DYNAR_DEFAULT_CAPACITY, alloc)
#define mkDynar(ty) mkDynarCA(ty, DYNAR_DEFAULT_CAPACITY, ALLOC)

// TODO: swap dynar and ty places, this is getting annoying
#define dynar_append(dynar, ty, _value, result) { \
    ty ___value = (_value); \
    bool _ = true; \
    result = true; \
    if(_) {} \
    dynar_init(dynar); \
    if((dynar)->len * (dynar)->element < (dynar)->mem.len) { \
        *((ty *)((dynar)->mem.s) + (dynar)->len) = ___value; \
        (dynar)->len++; \
    } \
    else if((dynar)->alloc == null){ \
        result = false; \
    } \
    else { \
        usz newCap = (dynar)->mem.len * 2; \
        if(newCap == 0) newCap = DYNAR_DEFAULT_CAPACITY * (dynar)->element; \
        Mem newMem = AllocateBytesC((dynar)->alloc, newCap); \
        mem_copy(newMem, (dynar)->mem); \
        (dynar)->mem = newMem; \
        *((ty *)(dynar)->mem.s + (dynar)->len) = ___value; \
        (dynar)->len++; \
    } \
} \

bool dynar_append_clone(Dynar(Mem) *dynar, Mem mem) {
    mem = mem_clone(mem, dynar->alloc);
    bool result = false;
    dynar_append(dynar, Mem, mem, result);
    return result;
}

#define dynar_foreach(ty, dynar) \
    for(struct { usz index; ty it; ty *itptr; } loop = { \
            .index = 0, \
            .it = ((dynar)->len > 0 ? dynar_index(ty, (dynar), 0) : (ty){}), \
            .itptr = ((dynar)->len > 0 ? &dynar_index(ty, (dynar), 0) : null) \
        }; \
        loop.index < (dynar)->len; \
            ++loop.index, \
            loop.it = (loop.index < (dynar)->len ? dynar_index(ty, (dynar), loop.index) : (ty){0}), \
            loop.itptr = (loop.index < (dynar)->len ? &dynar_index(ty, (dynar), loop.index) : null) \
        )

bool dynar_containsString(Dynar(String) *dynar, String value) {
    dynar_foreach(String, dynar) {
        if(mem_eq(loop.it, value)) return true;
    }
    return false;
}

#define dynar_reverse(ty, dynar) \
{ \
    usz lhs = 0; \
    usz rhs = (dynar)->len - 1; \
    while(lhs < rhs) { \
        ty temp = dynar_index((ty), (dynar), lhs); \
        dynar_index((ty), (dynar), lhs) = dynar_index((ty), (dynar), rhs); \
        dynar_index((ty), (dynar), rhs) = temp; \
        lhs += 1; \
        rhs -= 1; \
    } \
}

#endif // __LIB_DYNAR
