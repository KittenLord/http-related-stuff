#ifndef __LIB_GZIP
#define __LIB_GZIP

// Based on RFC-1952
// https://datatracker.ietf.org/doc/html/rfc1952

#include <types.h>
#include <stream.h>
#include "deflate.c"

#define GZIP_FLAG_TEXT 0
#define GZIP_FLAG_CRC16 1
#define GZIP_FLAG_EXTRA 2
#define GZIP_FLAG_NAME 3
#define GZIP_FLAG_COMMENT 4

#define GZIP_COMPRESSION_METHOD_DEFLATE 8

#define GZIP_ID1 0x1f
#define GZIP_ID2 0x8b

#define GZIP_OS_FAT 0
#define GZIP_OS_AMIGA 1
#define GZIP_OS_VMS 2
#define GZIP_OS_UNIX 3
#define GZIP_OS_VM_CMS 4
#define GZIP_OS_ATARI_TOS 5
#define GZIP_OS_HPFS 6
#define GZIP_OS_MACINSTOSH 7
#define GZIP_OS_ZSYSTEM 8
#define GZIP_OS_CP_M 9
#define GZIP_OS_TOPS20 10
#define GZIP_OS_NTFS 11
#define GZIP_OS_QDOS 12
#define GZIP_OS_ACORN_RISCOS 13
#define GZIP_OS_UNKNOWN 255

u32 Gzip_crcTable[256] = {0};
bool Gzip_crcTableInitialized = false;
void Gzip_initializeCrcTable() {
    for(u32 n = 0; n < 256; n++) {
        u32 c = n;
        for(u32 k = 0; k < 8; k++) {
            if(c & 1) {
                c = 0xedb88320L ^ (c >> 1);
            }
            else {
                c = c >> 1;
            }
        }
        Gzip_crcTable[n] = c;
    }
    Gzip_crcTableInitialized = true;
}

u32 Gzip_updateCrc(u32 crc, Mem mem) {
    u32 c = crc ^ 0xffffffffL;
    if(!Gzip_crcTableInitialized) Gzip_initializeCrcTable();
    for(usz n = 0; n < mem.len; n++) {
        c = Gzip_crcTable[(c ^ mem.s[n]) & 0xff] ^ (c >> 8);
    }
    return c ^ 0xffffffffL;
}

#define Gzip_crc(mem) Gzip_updateCrc(0L, (mem))

// NOTE: Can be modified to work on Stream (do I care tho?)
bool Gzip_verifyCrc(Mem mem, u32 supposedCrc) {
    u32 crc = Gzip_crc(mem);
    return crc == supposedCrc;
}

typedef struct __attribute__((packed)) {
    byte id1;
    byte id2;
    byte compressionMethod;
    u8 flags;
    u32 modificationTime;
    byte extraFlags;
    byte operatingSystem;
} GzipMember;

Mem Gzip_compress(Mem mem, Alloc *alloc) {
    StringBuilder sb = mkStringBuilder();
    sb.alloc = alloc;
    Stream out_ = mkStreamSb(&sb);
    Stream *out = &out_;

    u8 flags = {0};

    GzipMember member = {
        .id1 = GZIP_ID1,
        .id2 = GZIP_ID2,
        .compressionMethod = GZIP_COMPRESSION_METHOD_DEFLATE,
        .flags = flags,
        .modificationTime = 0,
        .extraFlags = 4,
        .operatingSystem = GZIP_OS_UNIX,
    };

    ResultWrite result = stream_write(out, mkMem((byte *)&member, sizeof(GzipMember)));
    if(result.error || result.partial) return memnull;

    Mem compressed = Deflate_compress(mem, false, 0, ALLOC);
    if(isNull(compressed)) return memnull; 

    result = stream_write(out, compressed);
    if(result.error || result.partial) return memnull;

    u32 crc = Gzip_crc(mem);
    result = stream_write(out, mkMem((byte *)&crc, sizeof(u32)));
    if(result.error || result.partial) return memnull;

    u32 isize = *(u32 *)(&mem.len);
    result = stream_write(out, mkMem((byte *)&isize, sizeof(u32)));
    if(result.error || result.partial) return memnull;

    return sb_build(sb);
}

Mem Gzip_decompress(Mem mem, Alloc *alloc) {
    Stream in_ = mkStreamStr(mem);
    Stream *in = &in_;

    GzipMember member = {0};

    ResultRead result = stream_read(in, mkMem((byte *)&member, sizeof(GzipMember)));
    if(result.error || result.partial) return memnull;

    if(member.id1 != GZIP_ID1 || member.id2 != GZIP_ID2) return memnull;
    if(member.compressionMethod != GZIP_COMPRESSION_METHOD_DEFLATE) return memnull;

    printf("%x %x\n", member.id1, member.id2);

    bool flagText = (member.flags >> GZIP_FLAG_TEXT) & 1;
    bool flagCrc16 = (member.flags >> GZIP_FLAG_CRC16) & 1;
    bool flagExtra = (member.flags >> GZIP_FLAG_EXTRA) & 1;
    bool flagName = (member.flags >> GZIP_FLAG_NAME) & 1;
    bool flagComment = (member.flags >> GZIP_FLAG_COMMENT) & 1;
    printf("%d %d %d %d %d\n", flagText, flagCrc16, flagExtra, flagName, flagComment);
    printf("%d\n", member.flags);

    if(flagExtra) {
        u16 xlen = 0;
        result = stream_read(in, mkMem((byte *)&xlen, sizeof(u16)));
        if(result.error || result.partial) return memnull;

        for(int i = 0; i < xlen; i++) {
            stream_popChar(in);
        }
    }

    StringBuilder nameSb = mkStringBuilder();
    if(flagName) {
        MaybeChar c;
        while(isJust(c = stream_popChar(in)) && c.value != 0) {
            sb_appendChar(&nameSb, c.value);
        }
    }

    StringBuilder commentSb = mkStringBuilder();
    if(flagComment) {
        MaybeChar c;
        while(isJust(c = stream_popChar(in)) && c.value != 0) {
            sb_appendChar(&commentSb, c.value);
        }
    }

    if(flagCrc16) {
        u16 crc = 0;
        result = stream_read(in, mkMem((byte *)&crc, sizeof(u16)));
        if(result.error || result.partial) return memnull;

        // TODO: this is bad, need to improve stream.pos
        usz len = in_.i;
        Mem header = mkMem(mem.s, len);
        if(!Gzip_verifyCrc(header, crc)) return memnull;
    }

    DeflateDeCompResult dresult = Deflate_decompress(in, alloc);
    if(isNone(dresult)) return memnull;

    u32 crc = 0;
    result = stream_read(in, mkMem((byte *)&crc, sizeof(u32)));
    if(result.error || result.partial) return memnull;

    if(!Gzip_verifyCrc(dresult.mem, crc)) return memnull;

    u32 isize = 0;
    result = stream_read(in, mkMem((byte *)&isize, sizeof(u32)));
    if(result.error || result.partial) return memnull;

    if(isize != (dresult.mem.len & 0xffffffffL)) return memnull;

    return dresult.mem;
}

#endif // __LIB_GZIP
