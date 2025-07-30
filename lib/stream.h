#ifndef __LIB_STREAM
#define __LIB_STREAM

#include <unistd.h>

#include "str.h"
#include "types.h"
#include "runes.h"
#include "macros.h"

typedef u8 StreamType;
#define STREAM_INVALID 0
#define STREAM_STR 1
#define STREAM_FD 2
#define STREAM_FILE 3
#define STREAM_SB 4
#define STREAM_NULL 5
typedef struct Stream Stream;
struct Stream {
    StreamType type;

    bool hasPeek;
    byte peekChar;
    rune peekRune;

    bool wbufferEnabled;
    Mem wbuffer;
    usz wbufferTaken;

    bool rbufferEnabled;
    Mem rbuffer;
    usz rbufferSize;
    usz rbufferConsumed;

    bool wlimitEnabled;
    isz wlimit;

    bool rlimitEnabled;
    isz rlimit;

    usz pos;
    usz col;
    usz row;
    usz lastCol;
    bool preservePos;

    union {
        struct {
            String s;
            usz i;
        };

        struct {
            int fd;
        };

        struct {
            StringBuilder *sb;
            usz sbi;
        };
    };
};

#define mkStreamStr(str) ((Stream){ .type = STREAM_STR, .s = (str), .i = 0 })
#define mkStreamFd(_fd) ((Stream){ .type = STREAM_FD, .fd = (_fd) })
#define mkStreamSb(_sb) ((Stream){ .type = STREAM_SB, .sb = (_sb) })
#define mkStreamNull() ((Stream){ .type = STREAM_NULL })

#define CHAR_NONE 0
#define CHAR_EOF 0
#define CHAR_ERROR 1
typedef struct {
    byte value;
    bool error;
    u64 errmsg;
} MaybeByte;
typedef MaybeByte MaybeChar;

typedef struct {
    bool error;
    bool partial;
    usz written;
} ResultWrite;

typedef struct {
    bool error;
    bool partial;
    usz read;
} ResultRead;

#define mkResultWrite(intend, real) ((ResultWrite){ .written = (real), .partial = (real) < (intend) })
#define mkResultRead(intend, real) ((ResultRead){ .read = (real), .partial = (real) < (intend) })

bool flattenStreamResultWrite(ResultWrite result) { return !(result.error || result.partial); }
bool flattenStreamResultRead(ResultRead result) { return !(result.error || result.partial); }

void stream_wbufferEnableC(Stream *s, Mem buffer) {
    if(s->wbufferEnabled) return;

    s->wbufferEnabled = true;
    s->wbuffer = buffer;
    s->wbufferTaken = 0;
}

void stream_wbufferEnable(Stream *s, usz size) {
    if(s->wbufferEnabled) return;
    Mem wbuffer = AllocateBytes(size);
    stream_wbufferEnableC(s, wbuffer);
}

void stream_rbufferEnableC(Stream *s, Mem buffer) {
    if(s->rbufferEnabled) return;

    s->rbufferEnabled = true;
    s->rbuffer = buffer;
    s->rbufferSize = 0;
    s->rbufferConsumed = 0;
}

void stream_rbufferEnable(Stream *s, usz size) {
    if(s->rbufferEnabled) return;
    Mem rbuffer = AllocateBytes(size);
    stream_rbufferEnableC(s, rbuffer);
}

void stream_rlimitEnable(Stream *s, isz limit) {
    s->rlimitEnabled = true;
    s->rlimit = limit;
}

void stream_rlimitDisable(Stream *s) {
    s->rlimitEnabled = false;
    s->rlimit = 0;
}

void stream_wlimitEnable(Stream *s, isz limit) {
    s->rlimitEnabled = true;
    s->wlimit = limit;
}

void stream_wlimitDisable(Stream *s) {
    s->wlimitEnabled = false;
    s->wlimit = 0;
}

ResultWrite stream_writeRaw(Stream *s, Mem mem) {
    if(!s) return none(ResultWrite);

    if(false) {}
    else if(s->type == STREAM_FD) {
        isz written = write(s->fd, mem.s, mem.len);
        if(written < 0) return none(ResultWrite);
        return mkResultWrite(mem.len, (usz)written);
    }
    else if(s->type == STREAM_SB) {
        bool result = sb_appendMem(s->sb, mem);
        if(result) { return mkResultWrite(mem.len, mem.len); }
        else { return none(ResultWrite); } 
    }
    else {
        return none(ResultWrite);
    }
}

ResultWrite stream_writeFlush(Stream *s) {
    if(!s) return none(ResultWrite);
    if(!s->wbufferEnabled) return mkResultWrite(0, 0);

    ResultWrite result = stream_writeRaw(s, mkMem(s->wbuffer.s, s->wbufferTaken));

    if(result.partial) {
        mem_move(s->wbuffer, memIndex(s->wbuffer, result.written));
        s->wbufferTaken -= result.written;
    }
    else if(!result.error && !result.partial) {
        s->wbufferTaken = 0;
    }

    return result;
}

ResultWrite stream_write(Stream *s, Mem mem) {
    if(!s) return none(ResultWrite);

    if(s->wbufferEnabled) {
        if(s->wbufferTaken + mem.len < s->wbuffer.len) {
            mem_copy(memIndex(s->wbuffer, s->wbufferTaken), mem);
            s->wbufferTaken += mem.len;
            return mkResultWrite(mem.len, mem.len);
        }
        else {
            ResultWrite result = stream_writeFlush(s);
            if(result.error || result.partial) return result;

            return stream_writeRaw(s, mem);
        }
    }
    else {
        return stream_writeRaw(s, mem);
    }
}

ResultRead stream_readRaw(Stream *s, Mem mem) {
    if(!s) return none(ResultRead);

    if(false) {}
    else if(s->type == STREAM_FD) {
        isz bytesRead = read(s->fd, mem.s, mem.len);
        if(bytesRead < 0) return none(ResultRead);
        return mkResultRead(mem.len, (usz)bytesRead);
    }
    else if(s->type == STREAM_SB) {
        Mem src = memIndex(memLimit(s->sb->s, s->sb->len), s->sbi);
        src = memLimit(src, mem.len);
        mem_copy(mem, src);
        return mkResultRead(mem.len, src.len);
    }
    else if(s->type == STREAM_STR) {
        Mem src = memIndex(s->s, s->i);
        src = memLimit(src, mem.len);
        mem_copy(mem, src);
        s->i += src.len;
        return mkResultRead(mem.len, src.len);
    }
    else {
        return none(ResultRead);
    }
}

ResultRead stream_read(Stream *s, Mem mem) {
    if(!s) return none(ResultRead);

    Mem originalMem = mem;
    if(s->rlimitEnabled && (s->rlimit <= 0 || (usz)s->rlimit < mem.len)) {
        mem.len = s->rlimit >= 0 ? s->rlimit : 0;
    }

    if(s->rbufferEnabled) {
        if(s->rbufferSize - s->rbufferConsumed == 0) {
            ResultRead result = stream_readRaw(s, s->rbuffer);
            if(!result.error) { s->rbufferSize = result.read; s->rbufferConsumed = 0; }
            else { s->rbufferSize = 0; }
        }

        if(mem.len <= s->rbufferSize - s->rbufferConsumed) {
            mem_copy(mem, memIndex(s->rbuffer, s->rbufferConsumed));
            s->rbufferConsumed += mem.len;

            if(s->rlimitEnabled) { s->rlimit -= mem.len; }
            return mkResultRead(originalMem.len, mem.len);
        }
        else {
            Mem src = memLimit(s->rbuffer, s->rbufferSize);
            src = memIndex(src, s->rbufferConsumed);
            mem_copy(mem, src);
            s->rbufferConsumed = s->rbufferSize;

            mem = memIndex(mem, src.len);
            ResultRead result = stream_readRaw(s, mem);
            if(result.error) {
                return result;
            }

            usz bytesRead = result.read;

            if(s->rlimitEnabled) { s->rlimit -= src.len + bytesRead; }
            return mkResultRead(originalMem.len, src.len + bytesRead);
        }
    }
    else {
        ResultRead result = stream_readRaw(s, mem);
        if(s->rlimitEnabled) { s->rlimit -= result.read; }
        return result;
    }
}

MaybeChar stream_popChar(Stream *s) {
    if(!s) return none(MaybeChar);
    if(s->hasPeek) { s->hasPeek = false; return just(MaybeChar, s->peekChar); }

    byte b = 0;
    Mem m = mkMem(&b, 1);
    ResultRead result = stream_read(s, m);

    if(result.error) return fail(MaybeChar, CHAR_ERROR);
    if(result.partial) return fail(MaybeChar, CHAR_EOF);

    if(!s->preservePos) {
        s->pos++;
        if(b == '\n') { s->row++; s->lastCol = s->col; s->col = 0; }
        else          { s->col++; }
    }

    return just(MaybeChar, b);
}

MaybeRune stream_popRune(Stream *s) {
    if(!s) return none(MaybeRune);
    if(s->hasPeek) { s->hasPeek = false; return just(MaybeRune, s->peekRune); }

    byte data[4] = {0};
    int len = 0;
    s->preservePos = true;
    while(len < 4) {
        MaybeChar c = stream_popChar(s);
        if(isFail(c, CHAR_EOF) && len == 0) return none(MaybeRune);
        if(isNone(c)) return fail(MaybeRune, RUNE_INVALID);
        data[len] = c.value;
        len++;

        MaybeRune r = getRune(data, len);
        if(r.error && r.errmsg == RUNE_UNFINISHED) continue;
        else if(r.error && r.errmsg == RUNE_INVALID) break;

        s->pos++;
        if(r.value == '\n') { s->row++; s->lastCol = s->col; s->col = 0; }
        else                { s->col++; }
        s->preservePos = false; return r;
    }

    s->preservePos = false;
    return fail(MaybeRune, RUNE_INVALID);
}

bool stream_writeChar(Stream *s, byte c) {
    Mem m = mkMem(&c, 1);
    ResultWrite result = stream_write(s, m);
    return !result.error && !result.partial;
}

bool stream_writeRune(Stream *s, rune r) {
    i8 len = getRuneLen(r);
    byte *data = (byte *)&r;
    for(int i = 0; i < len; i++) {
        bool result = stream_writeChar(s, data[i]);
        if(!result) return false;
    }
    return true;
}

void stream_goBackOnePos(Stream *s) {
    if(s->col > 0) { s->col--; }
    else { s->row--; s->col = s->lastCol; }
}

MaybeChar stream_peekChar(Stream *s) {
    if(s->hasPeek) return just(MaybeChar, s->peekChar);
    MaybeChar c = stream_popChar(s);
    if(isNone(c)) return c;
    s->peekChar = c.value;
    s->hasPeek = true;
    return c;
}

MaybeRune stream_peekRune(Stream *s) {
    if(s->hasPeek) return just(MaybeRune, s->peekRune);
    MaybeRune r = stream_popRune(s);
    if(isNone(r)) return r;
    s->peekRune = r.value;
    s->hasPeek = true;
    return r;
}

MaybeChar stream_routeUntil(Stream *s, Stream *out, byte target, bool includeInResult, bool consumeLast) {
    MaybeChar c;
    while(isJust(c = stream_peekChar(s)) && c.value != target) {
        stream_popRune(s);
        stream_writeChar(out, c.value);
    }

    if(consumeLast) stream_popChar(s);
    if(consumeLast && includeInResult) stream_writeChar(out, c.value);
    c = stream_peekChar(s);
    return c;
}

MaybeChar stream_routeLine(Stream *s, Stream *out, bool includeNewLine) {
    stream_routeUntil(s, out, '\n', includeNewLine, true);
    MaybeChar c = stream_peekChar(s);
    return c;
}

bool stream_dumpInto(Stream *in, Stream *out, u64 max, bool returnFalseOnMax) {
    if(max == 0) max = u64max;
    ResultRead r;
    byte buffer[1024];
    while((r = stream_read(in, mkMem(buffer, max > 1024 ? 1024 : max))).read != 0) {
        if(max == 0 && returnFalseOnMax) return false;
        if(max == 0) return true;
        u64 toWrite = r.read;

        if(toWrite >= max) { toWrite = max; max = 0; }
        else { max -= toWrite; }

        ResultWrite rw = stream_write(out, mkMem(buffer, toWrite));
        if(rw.error) return false;
    }
    if(r.error) return false;
    return true;
}

Mem stream_dump(Stream *s, Alloc *alloc, u64 max, bool returnFalseOnMax) {
    StringBuilder sb = mkStringBuilder();
    sb.alloc = alloc;
    Stream sbs = mkStreamSb(&sb);
    bool result = stream_dumpInto(s, &sbs, max, returnFalseOnMax);
    if(!result) return memnull;
    return sb_build(sb);
}

#endif // __LIB_STREAM
