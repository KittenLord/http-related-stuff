#ifndef __LIB_MACROS
#define __LIB_MACROS

#define null ((void *)0)
#define BLOCK(b) do { b } while(0)

#define just(ty, v) ((ty){ .value = (v) })
#define justv(ty, f, v) ((ty){ .f = (v) })
#define none(ty) ((ty){ .error = true })
#define fail(ty, m) ((ty){ .error = true, .errmsg = (m) })

#define isJust(v) (!(v).error)
#define isNone(v) ((v).error)
#define isFail(v, f) ((v).error && (v).errmsg == (f))

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define DEBUG_LOC " " TOSTRING(__FILE__) ":" TOSTRING(__LINE__)

#endif // __LIB_MACROS
