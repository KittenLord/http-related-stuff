#ifndef __LIB_MAP
#define __LIB_MAP

#include <types.h>
#include <alloc.h>

typedef struct {
    Mem key;
    Mem val;

    MapNode *next;
} MapNode;

typedef struct {
    MapNode *nodes;
    Alloc *alloc;
} Map;

void map_set(Map *map, Mem key, Mem val) {
    if(!map->nodes) {
        map->nodes = AllocateBytesC(map->alloc, sizeof(MapNode));
        map->nodes->key = mem_clone(key, map->alloc);
        map->nodes->val = mem_clone(val, map->alloc);
        return;
    }

    MapNode *current = map->nodes;
    while(current != null && !mem_eq(current->key, key)) {
        current = current->next;
    }

    if(current == null) {
        MapNode *temp = map->nodes;
        map->nodes = null;
        map_set(map, key, val);
        map->nodes->next = temp;
        return;
    }
    
    FreeC(map->alloc, current->val.s);
    current->val = mem_clone(val, map->alloc);
    return;
}

Mem map_get(Map *map, Mem key) {
    MapNode *current = map->nodes;
    while(current != null && !mem_eq(current->key, key)) {
        current = current->next;
    }

    if(current == null) return (Mem){ .s = null, .len = 0 };
    return current->val;
}

void map_remove(Map *map, Mem key) {
    if(map->nodes == null) return;

    if(mem_eq(map->nodes->key, key)) {
        FreeC(map->alloc, map->nodes->key.s);
        FreeC(map->alloc, map->nodes->val.s);
        map->nodes = map->nodes->next;
        return;
    }

    MapNode *current = map->nodes;
    while(current->next != null && !mem_eq(current->next->key, key)) {
        current = current->next;
    }

    if(current->next == null) return;

    FreeC(map->alloc, current->next->key.s);
    FreeC(map->alloc, current->next->val.s);
    current->next = current->next->next;
    return;
}

bool map_has(Map *map, Mem key) {
    if(map->nodes == null) return false;

    MapNode *current = map->nodes;
    while(current != null && !mem_eq(current->key, key)) {
        current = current->next;
    }

    if(current == null) return false;
    else                return true;
}

#endif // __LIB_MAP
