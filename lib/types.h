#ifndef __LIB_TYPES
#define __LIB_TYPES

// TODO: remove this
#include <stdint.h>
#include <sys/types.h>

typedef  int8_t i8;
typedef uint8_t u8;

#define i8min -128
#define i8max 127
#define u8max 255

#define i8decmin -100
#define i8decmax 100
#define u8decmax 100

typedef  int16_t i16;
typedef uint16_t u16;

#define i16min -32768
#define i16max 32767
#define u16max 65535

#define i16decmin -10000
#define i16decmax 10000
#define u16decmax 10000

typedef  int32_t i32;
typedef uint32_t u32;

#define i32min -2147483648
#define i32max 2147483647
#define u32max 4294967295

#define i32decmin -1000000000
#define i32decmax 1000000000
#define u32decmax 1000000000

typedef  int64_t i64;
typedef uint64_t u64;

#define i64min -9223372036854775808LL
#define i64max 9223372036854775807LL
#define u64max 18446744073709551615ULL

#define i64decmin -1000000000000000000LL
#define i64decmax 1000000000000000000LL
#define u64decmax 10000000000000000000ULL

typedef ssize_t isz;
typedef size_t usz;

typedef double f64;
typedef float f32;

typedef u8 bool;
#define false 0
#define true 1

typedef u8 byte;
typedef u8 bit;

typedef u32 rune;
typedef void * ptr;

typedef struct {
    byte *s;
    usz len;
} Mem;

typedef Mem String;

#define memnull ((Mem){ .s = null, .len = 0 })
#define mkMem(_s, _len) ((Mem){ .s = (byte *)(_s), .len = (_len) })
// TODO: should this return true if len == 0?
#define isNull(mem) ((mem).s == null)

#endif // __LIB_TYPES
