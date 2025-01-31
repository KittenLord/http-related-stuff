#ifndef __LIB_HASHMAP
#define __LIB_HASHMAP

#include <types.h>
#include <alloc.h>

typedef struct {
    Alloc *alloc;
} Hashmap;

void hm_set(Hashmap *hm, Mem key, Mem val) {

}

#endif // __LIB_HASHMAP
