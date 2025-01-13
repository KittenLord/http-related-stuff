#ifndef __LIB_STR
#define __LIB_STR

// TODO: remove this
#include <string.h>
#include "types.h"

typedef struct {
    byte *s;
    usz len;
} str;

#define mstr(_s, _len) ((str){ .s = _s, .len = _len })
#define mstr0(_s) ((str){ .s = _s, .len = strlen(_s) })

#endif // __LIB_STR
