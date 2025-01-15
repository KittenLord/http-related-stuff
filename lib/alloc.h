#ifndef __LIB_ALLOC
#define __LIB_ALLOC

// TODO: figure out how to make an allocator stack per thread, or some other approach

// TODO: obviously remove these two
#include <stdlib.h>
#include <string.h>

#include "types.h"
#include "macros.h"

typedef struct Alloc Alloc;
struct Alloc {
    ptr  (*alloc)(Alloc *a, usz);
    void (*free)(Alloc *a, ptr);
    void (*reset)(Alloc *a);
    void (*kill)(Alloc *a);

    void *data;
};

ptr  malloc_alloc(Alloc *a, usz size) { return calloc(sizeof(byte), size); }
void malloc_free(Alloc *a, ptr p) { free(p); }
void malloc_reset(Alloc *a) {}
void malloc_kill(Alloc *a) {}

#define ALLOC_GLOBAL_DEF ((Alloc){ .alloc = malloc_alloc, .free = malloc_free, .reset = malloc_reset, .kill = malloc_kill })
Alloc ALLOC_GLOBAL_VALUE = ALLOC_GLOBAL_DEF;
Alloc *ALLOC_GLOBAL = &ALLOC_GLOBAL_VALUE;

Alloc ALLOC_STACK[256] = { ALLOC_GLOBAL_DEF, 0 };
usz ALLOC_INDEX = 0;

// TODO: maybe this'll be better as a pointer?
#define ALLOC ALLOC_STACK[ALLOC_INDEX]

void ALLOC_POP() { 
    if(ALLOC_INDEX == 0) return;
    ALLOC.kill(&ALLOC);
    ALLOC_INDEX--;
}

void ALLOC_PUSH(Alloc alloc) {
    ALLOC_INDEX++;
    ALLOC = alloc;
}

#define AllocateBytes(bytes) AllocateBytesC(&ALLOC, (bytes))
ptr AllocateBytesC(Alloc *alloc, usz bytes) {
    return alloc->alloc(alloc, bytes);
}

#define Free(ptr) FreeC(&ALLOC, (ptr))
void FreeC(Alloc *alloc, ptr p) {
    alloc->free(alloc, p);
}

#define Reset() ResetC(&ALLOC)
void ResetC(Alloc *alloc) {
    alloc->reset(alloc);
}

#define Kill() KillC(&ALLOC)
void KillC(Alloc *alloc) {
    alloc->kill(alloc);
}

#define AllocateVar(ty, res, obj) \
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

ptr  LinearExpandable_alloc(Alloc *ap, usz size) {
    Alloc a = *ap;
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
        byte *newPage = AllocateBytesC(ALLOC_GLOBAL, data->pageSize);
        *((ptr *)newPage) = data->page;
        data->page = newPage;
        data->offset = 0 + sizeof(ptr *);
        data->lastAlloc = null;
        data->lastAllocSize = 0;

        return LinearExpandable_alloc(ap, size);
    }
}

void LinearExpandable_free(Alloc *ap, ptr p) {
    if(p == null) return;
    Alloc a = *ap;

    Alloc_LinearExpadableData *data = a.data;
    if(data->lastAlloc == p) {
        data->offset -= data->lastAllocSize;
        data->lastAlloc = null;
        data->lastAllocSize = 0;
    }
}

void LinearExpandable_reset(Alloc *ap) {
    Alloc a = *ap;
    Alloc_LinearExpadableData *data = a.data;

    ptr *next = (ptr *)data->page;
    while(*next) {
        ptr toFree = *next;
        next = *((ptr *)toFree);
        FreeC(ALLOC_GLOBAL, toFree);
    }

    data->offset = 0 + sizeof(ptr *);
    data->lastAlloc = null;
    data->lastAllocSize = 0;
}

void LinearExpandable_kill(Alloc *a) {
    LinearExpandable_reset(a);
    FreeC(ALLOC_GLOBAL, (ptr)(a->data));
}

#define mkAlloc_LinearExpandable() mkAlloc_LinearExpandable_Cap(8192)
Alloc mkAlloc_LinearExpandable_Cap(usz page) {
    Alloc_LinearExpadableData data = {
        .pageSize = page,
        .page = AllocateBytesC(ALLOC_GLOBAL, page),
        .offset = 0 + sizeof(ptr *),
        .lastAlloc = null,
        .lastAllocSize = 0
    };
    Alloc_LinearExpadableData *pdata = AllocateBytesC(ALLOC_GLOBAL, sizeof(Alloc_LinearExpadableData));
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
