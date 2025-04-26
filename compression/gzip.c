#ifndef __LIB_GZIP
#define __LIB_GZIP

#include <types.h>
#include <stream.h>

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

typedef struct {
    unsigned int text : 1;
    unsigned int headerCrc : 1;
    unsigned int extra : 1;
    unsigned int name : 1;
    unsigned int comment : 1;
    unsigned int _reserved : 3;
} GzipFlags;

typedef struct {
    byte id1;
    byte id2;
    byte compressionMethod;
    GzipFlags flags;
    u32 modificationTime;
    byte extraFlags;
    byte operatingSystem;
} GzipMember;

#endif // __LIB_GZIP
