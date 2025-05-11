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
    Mem  (*alloc)(Alloc *a, usz);
    void (*free)(Alloc *a, ptr);
    void (*reset)(Alloc *a);
    void (*kill)(Alloc *a);

    void *data;
};

Mem  malloc_alloc(Alloc *a, usz size) {
    a = a; Mem m = mkMem(calloc(size, sizeof(byte)), size);
    // printf("---=== MALLOC %x %d\n", m.s, size);
    if(m.s == null) { return memnull; } return m; }
void malloc_free(Alloc *a, ptr p) {
    // printf("---=== FREE %x\n", p);
    a = a; free(p); }
void malloc_reset(Alloc *a) { a = a; }
void malloc_kill(Alloc *a) { a = a; }

#define ALLOC_GLOBAL_DEF (Alloc){ .alloc = malloc_alloc, .free = malloc_free, .reset = malloc_reset, .kill = malloc_kill }

GLOBAL Alloc          ALLOC_GLOBAL_VALUE = ALLOC_GLOBAL_DEF;
GLOBAL Alloc          *ALLOC_GLOBAL = &ALLOC_GLOBAL_VALUE;
GLOBAL __thread Alloc ALLOC_STACK[256] = { ALLOC_GLOBAL_DEF, {0} };
GLOBAL __thread usz   ALLOC_INDEX = 0;

// TODO: maybe this'll be better as a pointer?
#define ALLOC (&ALLOC_STACK[ALLOC_INDEX])

void ALLOC_POP() { 
    if(ALLOC_INDEX == 0) return;
    if(ALLOC->kill != null) ALLOC->kill(ALLOC);
    ALLOC_INDEX--;
}

Alloc *ALLOC_PUSH(Alloc alloc) {
    ALLOC_INDEX++;
    *ALLOC = alloc;
    return ALLOC;
}

void ALLOC_PUSH_DUMMY() {
    ALLOC_PUSH((Alloc){0});
}

#define AllocateBytes(bytes) AllocateBytesC(ALLOC, (bytes))
Mem AllocateBytesC(Alloc *alloc, usz bytes) {
    return alloc->alloc(alloc, bytes);
}

#define Free(ptr) FreeC(ALLOC, (ptr))
void FreeC(Alloc *alloc, ptr p) {
    alloc->free(alloc, p);
}

#define Reset() ResetC(ALLOC)
void ResetC(Alloc *alloc) {
    alloc->reset(alloc);
}

#define Kill() KillC(ALLOC)
void KillC(Alloc *alloc) {
    alloc->kill(alloc);
}

#define AllocateVarC(ty, res, obj, alloc) \
    ty *res = null; \
    { \
        ty temp = (obj); \
        ptr src = (ptr)&temp; \
        res = (ty *)(AllocateBytesC(alloc, sizeof(ty)).s); \
        memcpy((ptr)res, src, sizeof(ty)); \
    }

#define AllocateVar(ty, res, obj) AllocateVarC(ty, res, (obj), ALLOC)

#define UseAlloc(a, block) BLOCK({ Alloc ___temp = a; ALLOC_PUSH(___temp); { block; }; ALLOC_POP(); })

typedef struct {
    Mem page;
    usz initialPageSize;
    usz offset;

    Alloc *alloc;

    Mem lastAlloc;
} Alloc_LinearExpadableData;

Mem LinearExpandable_alloc(Alloc *a, usz size) {
    // TODO: zero out memory

    Alloc_LinearExpadableData *data = a->data;
    if(size > data->page.len - sizeof(ptr *)) {
        // TODO: figure out what to do here
        return memnull;
    }

    if(data->offset + size <= data->page.len) {
        Mem result = mkMem(data->page.s + data->offset, size);
        data->offset += size;
        data->lastAlloc = result;
        return result;
    }
    else {
        usz newLen = data->page.len; // TODO: maybe use initialPageSize?
        Mem newPage = AllocateBytesC(data->alloc, newLen);
        *((ptr *)newPage.s) = data->page.s;
        data->page = newPage;
        data->offset = 0 + sizeof(ptr *);
        data->lastAlloc = memnull;

        return LinearExpandable_alloc(a, size);
    }
}

void LinearExpandable_free(Alloc *a, ptr p) {
    if(p == null) return;

    Alloc_LinearExpadableData *data = a->data;
    if(data->lastAlloc.s == p) {
        data->offset -= data->lastAlloc.len;
        data->lastAlloc = memnull;
    }
}

void LinearExpandable_reset(Alloc *a) {
    Alloc_LinearExpadableData *data = a->data;

    ptr current = data->page.s;
    while(true) {
        ptr next = *(ptr *)current;
        if(!next) break;

        ptr toFree = current;
        current = next;
        FreeC(data->alloc, toFree);
    }

    data->page = mkMem(current, data->initialPageSize);
    *(ptr *)(data->page.s) = 0;
    data->offset = 0 + sizeof(ptr *);
    data->lastAlloc = memnull;
}

void LinearExpandable_kill(Alloc *a) {
    LinearExpandable_reset(a);
    FreeC(((Alloc_LinearExpadableData *)a->data)->alloc, (ptr)(((Alloc_LinearExpadableData *)a->data)->page.s));
    FreeC(((Alloc_LinearExpadableData *)a->data)->alloc, (ptr)(a->data));
}

#define mkAlloc_LinearExpandable() mkAlloc_LinearExpandableAC(ALLOC_GLOBAL, 8192)
#define mkAlloc_LinearExpandableA(alloc) mkAlloc_LinearExpandableAC((alloc), 8192)
#define mkAlloc_LinearExpandableC(size) mkAlloc_LinearExpandableAC(ALLOC_GLOBAL, (size))
Alloc mkAlloc_LinearExpandableAC(Alloc *alloc, usz pageSize) {
    Alloc_LinearExpadableData data = {
        .initialPageSize = pageSize,
        .page = AllocateBytesC(alloc, pageSize),
        .alloc = alloc,
        .offset = 0 + sizeof(ptr *),
        .lastAlloc = memnull,
    };

    // NOTE: I'm not sure why this is necessary, for some reason this wasn't
    // getting zeroed out, even though the global allocator uses calloc
    *(ptr *)(data.page.s) = null;

    AllocateVarC(Alloc_LinearExpadableData, pdata, data, alloc);

    // Alloc_LinearExpadableData *pdata = AllocateBytesC(alloc, sizeof(Alloc_LinearExpadableData));
    // *pdata = data;

    return (Alloc){
        .alloc = LinearExpandable_alloc,
        .free = LinearExpandable_free,
        .reset = LinearExpandable_reset,
        .kill = LinearExpandable_kill,

        .data = pdata,
    };
}

#endif // __LIB_ALLOC
