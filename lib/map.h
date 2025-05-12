#ifndef __LIB_MAP
#define __LIB_MAP

#include <pthread.h>
#include "types.h"
#include "alloc.h"
#include "mem.h"

typedef struct {
    Mem key;
    Mem val;
} MapEntry;

typedef struct {
    Dynar(MapEntry) map;
    Alloc *alloc;
    pthread_mutex_t lock;
} Map;
#define MAP(a, b) Map

typedef struct {
    Map *map;
    usz index;
} MapIter;

#define mkMap() mkMapA(ALLOC)
#define mkMapA(a) ((Map){ .map = mkDynarAI(MapEntry, (a), true), .alloc = (a), .lock = PTHREAD_MUTEX_INITIALIZER })

int map_lock(Map *map) {
    return pthread_mutex_lock(&map->lock);
}

int map_unlock(Map *map) {
    return pthread_mutex_unlock(&map->lock);
}

#define map_block(map) for(bool block = map_lock((map)) != 0; !block; block = (map_unlock((map)), true))
#define map_blockCond(map, b) for(bool block = ((b) && map_lock((map))), false; !block; block = true, ((b) && map_unlock((map))))

bool map_iter_end(MapIter *iter) {
    return iter->index >= iter->map->map.len;
}

MapEntry map_iter_next(MapIter *iter) {
    MapEntry entry = dynar_index(MapEntry, &iter->map->map, iter->index);
    iter->index++;
    return entry;
}

MapIter map_iter(Map *map) {
    return (MapIter){ .map = map, .index = 0 };
}

// NOTE: Adds a new element even if this same key already exists.
// map_get and others won't get to this, until ones before this
// get removed. Used for iteration
void map_setRepeat(Map *map, Mem key, Mem val) {
    key = mem_clone(key, map->alloc);
    val = mem_clone(val, map->alloc);
    dynar_append(&map->map, MapEntry, ((MapEntry){ .key = key, .val = val }), _);
}

void map_set(Map *map, Mem key, Mem val) {
    usz i = 0;
    for(i = 0; i < map->map.len; i++) {
        if(mem_eq(dynar_index(MapEntry, &map->map, i).key, key)) break;
    }
    bool found = i < map->map.len;
    if(found && mem_eq(dynar_index(MapEntry, &map->map, i).val, val)) return;
    val = mem_clone(val, map->alloc);
    if(found) {
        key = dynar_index(MapEntry, &map->map, i).key;
        dynar_set(MapEntry, &map->map, i, ((MapEntry){ .key = key, .val = val }));
    }
    else {
        key = mem_clone(key, map->alloc);
        dynar_append(&map->map, MapEntry, ((MapEntry){ .key = key, .val = val }), _);
    }
}

Mem map_get(Map *map, Mem key) {
    for(usz i = 0; i < map->map.len; i++) {
        MapEntry entry = dynar_index(MapEntry, &map->map, i);
        if(mem_eq(key, entry.key)) return entry.val;
    }
    return memnull;
}

void map_remove(Map *map, Mem key) {
    for(usz i = 0; i < map->map.len; i++) {
        MapEntry entry = dynar_index(MapEntry, &map->map, i);
        if(mem_eq(key, entry.key)) {
            dynar_remove(MapEntry, &map->map, i);
            return;
        }
    }
}

bool map_has(Map *map, Mem key) {
    return !isNull(map_get(map, key));
}

#endif // __LIB_MAP
