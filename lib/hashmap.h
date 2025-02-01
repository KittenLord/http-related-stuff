#ifndef __LIB_HASHMAP
#define __LIB_HASHMAP

#include "types.h"
#include "alloc.h"
#include "map.h"
#include "mem.h"

typedef struct {
    MapNode **nodes;

    usz len;

    usz max;
    usz total;

    Alloc *alloc;
} Hashmap;

#define mkHashmap(_alloc) ((Hashmap){ .alloc = (_alloc) })

// https://github.com/fabiogaluppo/fnv/blob/main/fnv64.hpp
// from here
usz fnv64hash(Mem m) {
    usz h = 0xcbf29ce484222325ULL;
    for(int i = 0; i < m.len; i++) {
        h += (h << 1) + (h << 4) + (h << 5) +
             (h << 7) + (h << 8) + (h << 40);
        h ^= (uint64_t)(m.s[i]);
    }
    return h;
}
// to here

void hm_fix(Hashmap *hm) {
    if(hm->nodes == null) {
        hm->nodes = AllocateBytesC(hm->alloc, sizeof(MapNode *) * 16);
        hm->len = 16;
        hm->max = 0;
        hm->total = 0;
        return;
    }

    // TODO: rebalancing if needed
}

usz hm_index(usz mod, Mem key) {
    usz hash = fnv64hash(key);
    return hash % mod;
}

void hm_set(Hashmap *hm, Mem key, Mem val) {
    hm_fix(hm);
    usz index = hm_index(hm->len, key);
    MapNode *nodes = hm->nodes[index];

    if(nodes == null) {
        hm->nodes[index] = AllocateBytesC(hm->alloc, sizeof(MapNode));
        nodes = hm->nodes[index];
        nodes->key = mem_clone(key, hm->alloc);
        nodes->val = mem_clone(val, hm->alloc);

        hm->total++;
        if(hm->max < 1) hm->max = 1;

        return;
    }

    if(mem_eq(nodes->key, key)) {
        FreeC(hm->alloc, nodes->val.s);
        nodes->val = mem_clone(val, hm->alloc);
        return;
    }

    MapNode *current = nodes;
    int depth = 1;
    while(current->next != null && !mem_eq(current->next->key, key)) {
        depth++;
        current = current->next;
    }

    if(current->next == null) {
        current->next = AllocateBytesC(hm->alloc, sizeof(MapNode));
        current->next->key = mem_clone(key, hm->alloc);
        current->next->val = mem_clone(val, hm->alloc);

        hm->total++;
        if(hm->max < depth) hm->max = depth;

        return;
    }

    FreeC(hm->alloc, nodes->val.s);
    current->next->val = mem_clone(val, hm->alloc);

    return;
}

Mem hm_get(Hashmap *hm, Mem key) {
    hm_fix(hm);
    usz index = hm_index(hm->len, key);
    MapNode *current = hm->nodes[index];
    while(current != null && !mem_eq(current->key, key)) {
        current = current->next;
    }

    if(current == null) return memnull;
    return current->val;
}

void hm_remove(Hashmap *hm, Mem key) {

}

bool hm_has(Hashmap *hm, Mem key) {
    return true;
}

#endif // __LIB_HASHMAP
