#ifndef __LIB_ALLOC
#define __LIB_ALLOC

#include <stdlib.h>
#include <string.h>

#include "types.h"
#include "macros.h"

typedef struct Alloc Alloc;
struct Alloc {
    ptr (*alloc)(Alloc, usz);
    void (*free)(Alloc, ptr);
    void (*kill)(Alloc);

    void *data;
};

ptr malloc_alloc(Alloc _, usz size) { return calloc(sizeof(byte), size); }
void malloc_free(Alloc _, ptr p) { free(p); }
void malloc_kill(Alloc _) {}

#define ALLOC_GLOBAL ((Alloc){ .alloc = malloc_alloc, .free = malloc_free, .kill = malloc_kill })

Alloc ALLOC_STACK[256] = { ALLOC_GLOBAL, 0 };
usz ALLOC_INDEX = 0;

#define ALLOC ALLOC_STACK[ALLOC_INDEX]

void ALLOC_POP() { 
    if(ALLOC_INDEX == 0) return;
    ALLOC.kill(ALLOC);
    ALLOC_INDEX--;
}

void ALLOC_PUSH(Alloc alloc) {
    ALLOC_INDEX++;
    ALLOC = alloc;
}

ptr AllocateBytes(usz bytes) {
    return ALLOC_STACK[ALLOC_INDEX].alloc(ALLOC_STACK[ALLOC_INDEX], bytes);
}

void Free(ptr p) {
    ALLOC.free(ALLOC, p);
}

#define Allocate(ty, res, obj) \
    ty *res = null; \
    { \
        ty temp = obj; \
        ptr src = (ptr)&temp; \
        res = (ty *)AllocateBytes(sizeof(ty)); \
        memcpy((ptr)res, src, sizeof(ty)); \
    }

#define UseAlloc(alloc, block) BLOCK({ ALLOC_PUSH(alloc); block; ALLOC_POP(); })

#endif // __LIB_ALLOC
