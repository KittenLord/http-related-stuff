#ifndef __LIB_STR
#define __LIB_STR

// TODO: remove this
#include <string.h>
#include "alloc.h"
#include "types.h"

typedef struct {
    byte *s;
    usz len;
} String;

typedef struct {
    byte *s;
    usz len;
    usz cap;

    Alloc *alloc;
} StringBuilder;

#define mstr(_s, _len) ((String){ .s = _s, .len = _len })
#define mstr0(_s) ((String){ .s = _s, .len = strlen(_s) })

#endif // __LIB_STR
