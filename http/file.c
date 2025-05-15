#ifndef __LIB_COIL_FILE
#define __LIB_COIL_FILE

#include <stdio.h>
#include <stdlib.h>

#include <stream.h>
#include <compression/gzip.c>
#include <compression/zlib.c>
#include <crypto/sha.c>

#include <map.h>
#include <hashmap.h>

typedef struct {
    bool error;

    Mem data;
    HttpMediaType mediaType;
    time_t modTime;

    Hash256 hash;
    bool hasHash;

    Mem gzip;
    Mem zlib;
} File;

typedef struct {
    Alloc *alloc;

    bool disableCaching;

    bool doHash;
    bool doGzip;
    bool doZlib;

    HASHMAP(String, File) hm;
} FileStorage;

typedef struct {
    Alloc *alloc;
    FileStorage *storage;
    UriPath basePath;
} FileTreeRouter;

HttpMediaType getMediaType(String extension) {
    if(isNull(extension)) return mkHttpMediaType("application", "octet-stream");

#define Ext(s) mem_eq(extension, mkString(s)) ||
#define MType(s, b, c) if(s false) { return mkHttpMediaType((b), (c)); }
    MType(Ext("jpeg") Ext("jpg"), "image", "jpeg")
    MType(Ext("png"), "image", "png")
    MType(Ext("html") Ext("htm"), "text", "html")
    MType(Ext("css"), "text", "css")
    MType(Ext("json"), "application", "json")
    MType(Ext("pdf"), "application", "pdf")

    // NOTE: as per RFC-2046, text/plain MUST have CRLF as newlines,
    // but I'm going to ignore that
    MType(Ext("txt"), "text", "plain")
#undef MType
#undef Ext

    return mkHttpMediaType("application", "octet-stream");
}

time_t getFileModTime(String path) {
    struct stat s = {0};
    int result = stat(fixchar path.s, &s);

    if(result != 0) return 0;
    return s.st_mtime;
}

String getFileExtension(String path) {
    usz i = 0;
    bool foundPeriod = false;
    for(i = path.len - 1; true /*|| i >= 0*/; i--) {
        if(i == path.len - 1 && path.s[i] == '.') break;
        if(path.s[i] == '/') break;
        if(path.s[i] == '.') {
            foundPeriod = true;
            break;
        }
        if(i == 0) break;
    }

    if(!foundPeriod) return memnull;
    return memIndex(path, i + 1);
}

// TODO: we probably need a getFileStream(), but I'm not sure how to
// handle close() of the fd

File getFile(String path, Alloc *alloc) {
    struct stat s = {0};
    int result = stat(fixchar path.s, &s);
    if(result != 0) return none(File);

    FILE *file = fopen(fixchar path.s, "r");
    if(file == null) return none(File);
    int fd = fileno(file);
    if(fd == -1) return none(File);

    Mem data = AllocateBytesC(alloc, s.st_size);
    isz bytesRead = read(fd, data.s, data.len);
    if(bytesRead < 0 || (usz)bytesRead != data.len) {
        FreeC(alloc, data.s);
        return none(File);
    }

    String extension = getFileExtension(path);
    HttpMediaType mediaType = getMediaType(extension);

    return (File){
        .data = data,
        .mediaType = mediaType,
        .modTime = s.st_mtime,
    };
}

// TODO: error checking
void storageFillFile(File *file, FileStorage *storage) {
    if(storage->doHash) {
        file->hash = Sha256(file->data);
        file->hasHash = true;
    }

    if(storage->doGzip) {
        file->gzip = Gzip_compress(file->data, storage->alloc);
    }

    if(storage->doZlib) {
        file->zlib = Zlib_compress(file->data, storage->alloc);
    }
}

File getFileStorage(String path, FileStorage *storage) {
    if(storage == null) {
        return getFile(path, ALLOC);
    }

    if(storage->disableCaching) {
        File file = getFile(path, ALLOC);
        if(isNone(file)) return none(File);
        storageFillFile(&file, storage);
        return file;
    }

    // NOTE: if the file has been deleted, we don't look for it (maybe change?)
    time_t modTime = getFileModTime(path);
    if(modTime == 0) return none(File);

    File file;
    Map *map = hm_getMap(&storage->hm, path);

    // TODO: this does not copy memory from hashmap to user, thus the
    // hashmap can't free old versions of files - which is fine, if the
    // files modify rarely (or at all), but I'm not sure if this is good
    map_block(map) {
        File *supposedFile = (File *)map_get(map, path).s;
        if(supposedFile != null && supposedFile->modTime == modTime) {
            file = *supposedFile;
            continue;
        }

        file = getFile(path, storage->alloc);
        if(isNone(file)) return none(File);
        storageFillFile(&file, storage);

        map_set(map, path, memPointer(File, &file));
    }

    return file;
}

File getFileTree(FileTreeRouter *ftrouter, UriPath subPath) {
    UriPath result = Uri_pathMoveRelatively(ftrouter->basePath, subPath, ALLOC);
    if(!Uri_pathHasPrefix(ftrouter->basePath, result)) return none(File);

    StringBuilder sb = mkStringBuilder();
    dynar_foreach(String, &result.segments) {
        sb_appendMem(&sb, loop.it);
        if(loop.index != result.segments.len - 1) {
            sb_appendChar(&sb, '/');
        }
    }

    String filePath = sb_build(sb);
    return getFileStorage(filePath, ftrouter->storage);
}

FileTreeRouter mkFileTreeRouter(String spath, FileStorage *storage) {
    Alloc *alloc = ALLOC;
    Stream s = mkStreamStr(spath);
    UriPath path = Uri_parsePathRootless(&s, alloc);
    // if(isJust(stream_peekChar(&s))) {
    //     // NOTE: error
    // }

    FileTreeRouter ftrouter = {
        .alloc = alloc,
        .basePath = path,
        .storage = storage,
    };

    return ftrouter;
}

FileStorage mkFileStorage(Alloc *alloc) {
    FileStorage storage = {
        .alloc = alloc,
        .hm = mkHashmap(alloc),

        .doHash = true,
        .doGzip = true,
        .doZlib = true,
    };

    hm_fix(&storage.hm);
    return storage;
}

#endif // __LIB_COIL_FILE
