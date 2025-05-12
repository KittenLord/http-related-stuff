#ifndef __LIB_HASHMAP
#define __LIB_HASHMAP

#include "types.h"
#include "alloc.h"
#include "map.h"
#include "mem.h"

typedef struct {
    Dynar(Map) map;
    Alloc *alloc;
} Hashmap;
#define HASHMAP(a, b) Hashmap

#define mkHashmap(_alloc) ((Hashmap){ .alloc = (_alloc) })

// https://github.com/fabiogaluppo/fnv/blob/main/fnv64.hpp
// from here
usz fnv64hash(Mem m) {
    usz h = 0xcbf29ce484222325ULL;
    for(usz i = 0; i < m.len; i++) {
        h += (h << 1) + (h << 4) + (h << 5) +
             (h << 7) + (h << 8) + (h << 40);
        h ^= (uint64_t)(m.s[i]);
    }
    return h;
}
// to here

void hm_fix(Hashmap *hm) {
    if(isNull(hm->map.mem)) {
        hm->map = mkDynarCA(Map, 32, hm->alloc);
        for(int i = 0; i < 32; i++) {
            dynar_append(&hm->map, Map, mkMapA(hm->alloc), _);
        }
    }

    // TODO: rebalancing if needed
}

usz hm_index(usz mod, Mem key) {
    usz hash = fnv64hash(key);
    return hash % mod;
}

Map *hm_getMap(Hashmap *hm, Mem key) {
    hm_fix(hm);
    usz index = hm_index(hm->map.len, key);

    Map *map = &dynar_index(Map, &hm->map, index);
    return map;
}

void hm_set(Hashmap *hm, Mem key, Mem val) {
    map_set(hm_getMap(hm, key), key, val);
}

Mem hm_get(Hashmap *hm, Mem key) {
    return map_get(hm_getMap(hm, key), key);
}

void hm_remove(Hashmap *hm, Mem key) {
    map_remove(hm_getMap(hm, key), key);
}

bool hm_has(Hashmap *hm, Mem key) {
    return map_has(hm_getMap(hm, key), key);
}

#endif // __LIB_HASHMAP
