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

typedef struct {
    char peek;
    rune peekRune;

    bool peekAvailable;

    Stream s;
} PeekStream;

#define mkPeekStream(str) ((PeekStream){ .peekAvailable = false, .s = str })

#define CHAR_NONE 0
#define CHAR_EOF 0
#define CHAR_ERROR 1
typedef struct {
    char value;
    bool error;
    u64 errmsg;
} MaybeChar;

bool stream_writeChar(Stream *s, char c);
usz stream_popChars(byte *dst, Stream *src, usz n);

MaybeChar stream_popChar(Stream *s) {
    if(s->type == STREAM_STR) {
        if(s->i >= s->s.len) return none(MaybeChar);
        char c = s->s.s[s->i++];
        if(!s->preservePos) { 
            s->pos++;
            if(c == '\n') { s->row++; s->lastCol = s->col; s->col = 0; }
            else          { s->col++; }
        }
        return just(MaybeChar, c);
    }
    else if(s->type == STREAM_FD) {
        char c;
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

bool stream_writeChar(Stream *s, char c) {
    if(!s) return false;
    if(s->type == STREAM_STR) {
        if(s->i >= s->s.len) return false;
        s->s.s[s->i++] = c;
        return true;
    }
    else if(s->type == STREAM_FD) {
        write(s->fd, &c, 1);
    }
    else if(s->type == STREAM_SB) {
        sb_appendChar(s->sb, c);
    }
    else if(s->type == STREAM_NULL) {
        return true;
    }
    else {
        return false;
    }
}

// TODO: is it possible to undo write, if not the whole rune had been written?
// maybe add a
//      usz stream_getAvailableLength(Stream *s);
bool stream_writeRune(Stream *s, rune r) {
    i8 len = getRuneLen(r);
    char *data = (char *)&r;
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

MaybeRune stream_popRune(Stream *s) {
    char data[4] = {0};
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

MaybeChar pstream_peekChar(PeekStream *s) {
    if(s->peekAvailable) return just(MaybeChar, s->peek);
    MaybeChar c = stream_popChar(&s->s);
    if(c.error) return c;
    s->peek = c.value;
    s->peekAvailable = true;
    return c;
}

MaybeChar pstream_popChar(PeekStream *s) {
    if(s->peekAvailable) { s->peekAvailable = false; return just(MaybeChar, s->peek); }
    return stream_popChar(&s->s);
}

MaybeRune pstream_peekRune(PeekStream *s) {
    if(s->peekAvailable) return just(MaybeRune, s->peekRune);
    MaybeRune r = stream_popRune(&s->s);
    if(r.error) return r;
    s->peekRune = r.value;
    s->peekAvailable = true;
    return r;
}

MaybeRune pstream_popRune(PeekStream *s) {
    if(s->peekAvailable) { s->peekAvailable = false; return just(MaybeRune, s->peekRune); }
    return stream_popRune(&s->s);
}

MaybeChar pstream_routeUntil(PeekStream *s, Stream *out, char target, bool includeInResult, bool consumeLast) {
    MaybeChar c;
    while(isJust(c = pstream_peekChar(s)) && c.value != target) {
        pstream_popRune(s);
        stream_writeChar(out, c.value);
    }

    if(consumeLast) pstream_popChar(s);
    if(consumeLast && includeInResult) stream_writeChar(out, c.value);
    c = pstream_peekChar(s);
    return c;
}

MaybeChar pstream_routeLine(PeekStream *s, Stream *out, bool includeNewLine) {
    pstream_routeUntil(s, out, '\n', includeNewLine, true);
    MaybeChar c = pstream_peekChar(s);
    return c;
}

#endif // __LIB_STREAM
