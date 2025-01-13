#ifndef __LIB_STREAM
#define __LIB_STREAM

#include "str.h"
#include "types.h"

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
    // TODO: change to rune?
    char peek;
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

MaybeChar stream_popchar(stream *s) {
    if(s->type == STREAM_STR) {
        if(s->i >= s->s.len) return nonec();
        return justc(s->s.s[s->i++]);
    }
    else {
        return nonec();
    }
}

MaybeChar pstream_peek(PeekStream *s) {
    if(s->peekAvailable) return justc(s->peek);
    MaybeChar c = stream_popchar(&s->s);
    if(c.error) return c;
    s->peek = c.value;
    s->peekAvailable = true;
    return c;
}

MaybeChar pstream_pop(PeekStream *s) {
    if(s->peekAvailable) { s->peekAvailable = false; return justc(s->peek); }
    return stream_popchar(&s->s);
}

#endif // __LIB_STREAM
