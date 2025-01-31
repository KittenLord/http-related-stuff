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
#define STREAM_BUF 6
typedef struct Stream Stream;
struct Stream {
    StreamType type;

    bool hasPeek;
    byte peekChar;
    rune peekRune;

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
        };

        struct {
            Stream *bufbase;
            byte *buf;
            usz ibuf;
            usz cbuf;
            usz maxbuf;
        };
    };
};

#define mkStreamStr(str) ((Stream){ .type = STREAM_STR, .s = (str), .i = 0 })
#define mkStreamFd(_fd) ((Stream){ .type = STREAM_FD, .fd = (_fd) })
#define mkStreamSb(_sb) ((Stream){ .type = STREAM_SB, .sb = (_sb) })
#define mkStreamNull() ((Stream){ .type = STREAM_NULL })
#define mkStreamBuf(base, size) ((Stream){ .type = STREAM_BUF, .bufbase = (base), .buf = AllocateBytes((size)), .ibuf = 0, .cbuf = 0, .maxbuf = (size) })

#define CHAR_NONE 0
#define CHAR_EOF 0
#define CHAR_ERROR 1
typedef struct {
    byte value;
    bool error;
    u64 errmsg;
} MaybeChar;


MaybeChar stream_popChar(Stream *s);
MaybeRune stream_popRune(Stream *s);

bool stream_writeChar(Stream *s, byte c);
bool stream_writeRune(Stream *s, rune r);

void stream_goBackOnePos(Stream *s);

MaybeChar stream_peekChar(Stream *s);
MaybeRune stream_peekRune(Stream *s);

usz stream_popChars(byte *dst, Stream *src, usz n);
usz stream_writeChars(Stream *dst, byte *src, usz n);

MaybeChar stream_routeUntil(Stream *s, Stream *out, byte target, bool includeInResult, bool consumeLast);
MaybeChar stream_routeLine(Stream *s, Stream *out, bool includeNewLine);


MaybeChar stream_popChar(Stream *s) {
    if(!s) return none(MaybeChar);
    if(s->hasPeek) { s->hasPeek = false; return just(MaybeChar, s->peekChar); }

    if(s->type == STREAM_STR) {
        if(s->i >= s->s.len) return none(MaybeChar);
        byte c = s->s.s[s->i++];
        if(!s->preservePos) { 
            s->pos++;
            if(c == '\n') { s->row++; s->lastCol = s->col; s->col = 0; }
            else          { s->col++; }
        }
        return just(MaybeChar, c);
    }
    else if(s->type == STREAM_FD) {
        byte c;
        isz result = read(s->fd, &c, 1);
        if(result <= 0) return fail(MaybeChar, CHAR_ERROR);
        return just(MaybeChar, c);
    }
    else if(s->type == STREAM_BUF) {
        if(s->ibuf < s->cbuf) {
            return just(MaybeChar, s->buf[s->ibuf++]);
        }

        s->cbuf = stream_popChars(s->buf, s->bufbase, s->maxbuf);
        s->ibuf = 0;
        if(s->cbuf == 0) return fail(MaybeChar, CHAR_EOF);
        return just(MaybeChar, s->buf[s->ibuf++]);
    }
    else {
        return fail(MaybeChar, CHAR_ERROR);
    }
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
    if(!s) return false;
    if(s->type == STREAM_STR) {
        if(s->i >= s->s.len) return false;
        s->s.s[s->i++] = c;
        return true;
    }
    else if(s->type == STREAM_FD) {
        write(s->fd, &c, 1);
        return true;
    }
    else if(s->type == STREAM_SB) {
        sb_appendChar(s->sb, c);
        return true;
    }
    else if(s->type == STREAM_NULL) {
        return true;
    }
    else {
        return false;
    }
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

// TODO: I'm not sure this is a good API - I think of smth
// like this:
//
// Stream sin, sout
// usz amount
// buffin = getBuffIn(sin, amount)
// buffout = getBuffOut(sout, amount)
// if(buffin && buffout)    memcpy(buffout, buffin, amount)
// else if(buffin)          writeChars(sout, buffin, amount)
// else if(buffout)         popChars(buffout, sin, amount)
// else                     // manual while loop
//
// of course, this needs to account for getBuffIn and
// getBuffOut routines not being able to provide the buffer
// of the required size

usz stream_popChars(byte *dst, Stream *src, usz n) {
    usz o = n;
    if(src->type == STREAM_FD) {
        return read(src->fd, dst, n);
    }
    else {
        MaybeChar c;
        while(n-- && 
              isJust(c = stream_popChar(src))) {
            *dst = c.value;
            dst++;
        }
        return o - n - 1;
    }
}

usz stream_writeChars(Stream *dst, byte *src, usz n) {
    usz o = n;
    if(dst->type == STREAM_FD) {
        return write(dst->fd, src, n);
    }
    else {
        while(n-- && 
              stream_writeChar(dst, *src)) {
            src++;
        }
        return o - n - 1;
    }
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

#endif // __LIB_STREAM
