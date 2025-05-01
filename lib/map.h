#ifndef __LIB_MAP
#define __LIB_MAP

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
} Map;

typedef struct {
    Map *map;
    usz index;
} MapIter;

#define mkMap() mkMapA(&ALLOC)
#define mkMapA(a) ((Map){ .map = mkDynarA(MapEntry, a), .alloc = (a) })

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
