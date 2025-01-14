#ifndef __LIB_ALLOC
#define __LIB_ALLOC

// TODO: obviously remove these two
#include <stdlib.h>
#include <string.h>

#include "types.h"
#include "macros.h"

typedef struct Alloc Alloc;
struct Alloc {
    ptr  (*alloc)(Alloc, usz);
    void (*free)(Alloc, ptr);
    void (*reset)(Alloc);
    void (*kill)(Alloc);

    void *data;
};

ptr  malloc_alloc(Alloc _, usz size) { return calloc(sizeof(byte), size); }
void malloc_free(Alloc _, ptr p) { free(p); }
void malloc_reset(Alloc _) {}
void malloc_kill(Alloc _) {}

#define ALLOC_GLOBAL ((Alloc){ .alloc = malloc_alloc, .free = malloc_free, .reset = malloc_reset, .kill = malloc_kill })

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

#define UseAlloc(a, block) BLOCK({ Alloc ___temp = a; ALLOC_PUSH(___temp); { block; }; ALLOC_POP(); })

typedef struct {
    usz pageSize;

    byte *page;
    usz offset;

    ptr lastAlloc;
    usz lastAllocSize;
} Alloc_LinearExpadableData;

ptr  LinearExpandable_alloc(Alloc a, usz size) {
    Alloc_LinearExpadableData *data = a.data;
    if(size > data->pageSize) return null; // TODO: figure out what to do here

    if(data->offset + size <= data->pageSize) {
        ptr result = data->page + data->offset;
        data->offset += size;
        data->lastAlloc = result;
        data->lastAllocSize = size;
        return result;
    }
    else {
        byte *newPage = ALLOC_GLOBAL.alloc(ALLOC_GLOBAL, data->pageSize);
        *((ptr *)newPage) = data->page;
        data->page = newPage;
        data->offset = 0 + sizeof(ptr *);
        data->lastAlloc = null;
        data->lastAllocSize = 0;

        return LinearExpandable_alloc(a, size);
    }
}

void LinearExpandable_free(Alloc a, ptr p) {
    if(p == null) return;

    Alloc_LinearExpadableData *data = a.data;
    if(data->lastAlloc == p) {
        data->offset -= data->lastAllocSize;
        data->lastAlloc = null;
        data->lastAllocSize = 0;
    }
}

void LinearExpandable_reset(Alloc a) {
    Alloc_LinearExpadableData *data = a.data;

    ptr *next = (ptr *)data->page;
    while(*next) {
        ptr toFree = *next;
        next = *((ptr *)toFree);
        ALLOC_GLOBAL.free(ALLOC_GLOBAL, toFree);
    }

    data->offset = 0 + sizeof(ptr *);
    data->lastAlloc = null;
    data->lastAllocSize = 0;
}

void LinearExpandable_kill(Alloc a) {
    LinearExpandable_reset(a);
    ALLOC_GLOBAL.free(ALLOC_GLOBAL, (ptr)(a.data));
}

#define mkAlloc_LinearExpandable() mkAlloc_LinearExpandable_Cap(8192)
Alloc mkAlloc_LinearExpandable_Cap(usz page) {
    Alloc_LinearExpadableData data = {
        .pageSize = page,
        .page = ALLOC_GLOBAL.alloc(ALLOC_GLOBAL, page),
        .offset = 0 + sizeof(ptr *),
        .lastAlloc = null,
        .lastAllocSize = 0
    };
    Alloc_LinearExpadableData *pdata = ALLOC_GLOBAL.alloc(ALLOC_GLOBAL, sizeof(Alloc_LinearExpadableData));
    *pdata = data;

    return (Alloc){
        .alloc = LinearExpandable_alloc,
        .free = LinearExpandable_free,
        .reset = LinearExpandable_reset,
        .kill = LinearExpandable_kill,

        .data = pdata
    };
}


#endif // __LIB_ALLOC
