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
    };
};

#define mkStreamStr(str) ((Stream){ .type = STREAM_STR, .s = (str), .i = 0 })
#define mkStreamFd(_fd) ((Stream){ .type = STREAM_FD, .fd = (_fd) })

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
    else {
        return fail(MaybeChar, CHAR_ERROR);
    }
}

bool stream_writeChar(Stream *s, char c) {
    if(s->type == STREAM_STR) {
        if(s->i >= s->s.len) return false;
        s->s.s[s->i++] = c;
        return true;
    }
    else if(s->type == STREAM_FD) {
        write(s->fd, &c, 1);
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

#endif // __LIB_STREAM
