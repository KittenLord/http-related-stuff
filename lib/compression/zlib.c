#ifndef __LIB_ZLIB
#define __LIB_ZLIB

// Based on RFC-1950
// https://datatracker.ietf.org/doc/html/rfc1950

#include <types.h>
#include <stream.h>
#include "deflate.c"

#define ZLIB_METHOD_DEFLATE 8

#define ZLIB_LEVEL_FASTEST 0
#define ZLIB_LEVEL_FAST 1
#define ZLIB_LEVEL_DEFAULT 2
#define ZLIB_LEVEL_SLOWEST 3

#define ZLIB_ADLER_BASE 65521

typedef struct __attribute__((packed)) {
    byte cmf;
    byte flags;
} ZlibStream;

u32 Zlib_adler32(Mem mem) {
    u16 adlerS1 = 1;
    u16 adlerS2 = 0;
    for(usz i = 0; i < mem.len; i++) {
        adlerS1 = (adlerS1 + mem.s[i]) % ZLIB_ADLER_BASE;
        adlerS2 = (adlerS2 + adlerS1) % ZLIB_ADLER_BASE;
    }

    u32 result = ((adlerS2 >> 8) & 0xff)
               | (((adlerS2 >> 0) & 0xff) << 8)
               | (((adlerS1 >> 8) & 0xff) << 16)
               | (((adlerS1 >> 0) & 0xff) << 24);
   
    return result;
}

Mem Zlib_compress(Mem mem, Alloc *alloc) {
    StringBuilder sb = mkStringBuilder();
    sb.alloc = alloc;
    Stream out_ = mkStreamSb(&sb);
    Stream *out = &out_;

    byte compressionMethod = ZLIB_METHOD_DEFLATE;
    byte compressionInfo = 7; // compression window
    byte cmf = (compressionMethod & 0b1111) 
             | ((compressionInfo & 0b1111) << 4);

    byte compressionLevel = ZLIB_LEVEL_DEFAULT;
    byte presetDictionary = 0; // false
    byte flags = ((presetDictionary & 0b1) << 5)
               | ((compressionLevel & 0b11) << 6);

    u16 fcheck = ((u16)cmf * 256)
               + ((u16)flags * 1);
    while(fcheck % 31 != 0) {
        flags += 1;
        fcheck = ((u16)cmf * 256)
               + ((u16)flags * 1);
    }

    ZlibStream zs = {
        .cmf = cmf,
        .flags = flags
    };

    ResultWrite result = stream_write(out, mkMem((byte *)&zs, sizeof(ZlibStream)));
    if(result.error || result.partial) return memnull;

    Mem compressed = Deflate_compress(mem, false, 0, alloc);
    if(isNull(compressed)) return memnull;

    result = stream_write(out, compressed);
    if(result.error || result.partial) return memnull;

    u32 adler32 = Zlib_adler32(mem);
    result = stream_write(out, mkMem((byte *)&adler32, sizeof(u32)));
    if(result.error || result.partial) return memnull;

    return sb_build(sb);
}

Mem Zlib_decompress(Mem mem, Alloc *alloc) {
    Stream in_ = mkStreamStr(mem);
    Stream *in = &in_;

    ZlibStream zs = {0};
    ResultRead result = stream_read(in, mkMem((byte *)&zs, sizeof(ZlibStream)));
    if(result.error || result.partial) return memnull;

    if(((u16)zs.cmf * 256 + (u16)zs.flags) % 31 != 0) return memnull;

    byte compressionMethod = zs.cmf & 0b1111;
    byte compressionInfo = (zs.cmf >> 4) & 0b1111;
    compressionInfo = compressionInfo;
    if(compressionMethod != ZLIB_METHOD_DEFLATE) return memnull;

    bool presetDictionary = ((zs.flags >> 5) & 0b1) == 1;
    u8 compressionLevel = (zs.flags >> 6) & 0b11;
    compressionLevel = compressionLevel;

    if(presetDictionary) {
        u32 dictid = 0;
        result = stream_read(in, mkMem((byte *)&dictid, sizeof(u32)));
        if(result.error || result.partial) return memnull;
    }

    DeflateDeCompResult decompressed = Deflate_decompress(in, alloc);
    if(isNone(decompressed)) return memnull;

    u32 adler32 = 0;
    result = stream_read(in, mkMem((byte *)&adler32, sizeof(u32)));
    if(result.error || result.partial) return memnull;

    if(adler32 != Zlib_adler32(decompressed.mem)) return memnull;

    return decompressed.mem;
}

#endif // __LIB_ZLIB
