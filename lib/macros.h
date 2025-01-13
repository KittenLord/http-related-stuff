#ifndef __LIB_MACROS
#define __LIB_MACROS

#define null ((void *)0)
#define BLOCK(b) do { b } while(0)

#define just(ty, v) ((ty){ .value = (v) })
#define none(ty) ((ty){ .error = true })
#define fail(ty, m) ((ty){ .error = true, .errmsg = (m) })

#endif // __LIB_MACROS
