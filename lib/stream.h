#ifndef __LIB_STREAM
#define __LIB_STREAM

#include "str.h"
#include "types.h"
#include "runes.h"

// TODO: holy fuck I need to figure out which naming convention to use, this is atrocious
typedef u8 stream_type;
#define STREAM_INVALID 0
#define STREAM_STR 1
#define STREAM_FD 2
#define STREAM_FILE 3
typedef struct stream stream;
struct stream {
    stream_type type;
    union {
        struct {
            str s;
            usz i;
        };

        struct {
            int fd;
        };
    };
};

#define mkStreamStr(str) ((stream){ .type = STREAM_STR, .s = (str), .i = 0 })

typedef struct {
    char peek;
    rune peekRune;

    bool peekAvailable;

    stream s;
} PeekStream;

#define mkPeekStream(str) ((PeekStream){ .peekAvailable = false, .s = str })

typedef struct {
    char value;
    bool error;
} MaybeChar;

#define justc(c) ((MaybeChar){ .value = (c), .error = false })
#define nonec() ((MaybeChar){ .error = true })

MaybeChar stream_popChar(stream *s) {
    if(s->type == STREAM_STR) {
        if(s->i >= s->s.len) return nonec();
        return justc(s->s.s[s->i++]);
    }
    else {
        return nonec();
    }
}

MaybeRune stream_popRune(stream *s) {
    char data[4] = {0};
    int len = 0;
    while(len < 4) {
        MaybeChar c = stream_popChar(s);
        if(c.error) return noner(RUNE_INVALID);
        data[len] = c.value;
        len++;

        MaybeRune r = getRune(data, len);
        if(r.error) continue;
        return r;
    }
    return noner(RUNE_INVALID);
}

MaybeChar pstream_peek(PeekStream *s) {
    if(s->peekAvailable) return justc(s->peek);
    MaybeChar c = stream_popChar(&s->s);
    if(c.error) return c;
    s->peek = c.value;
    s->peekAvailable = true;
    return c;
}

MaybeChar pstream_pop(PeekStream *s) {
    if(s->peekAvailable) { s->peekAvailable = false; return justc(s->peek); }
    return stream_popChar(&s->s);
}

MaybeRune pstream_peekRune(PeekStream *s) {
    if(s->peekAvailable) return justr(s->peekRune);
    MaybeRune r = stream_popRune(&s->s);
    if(r.error) return r;
    s->peekRune = r.value;
    s->peekAvailable = true;
    return r;
}

MaybeRune pstream_popRune(PeekStream *s) {
    if(s->peekAvailable) { s->peekAvailable = false; return justr(s->peekRune); }
    return stream_popRune(&s->s);
}

#endif // __LIB_STREAM
