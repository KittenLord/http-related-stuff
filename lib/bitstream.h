#ifndef __LIB_BISTREAM
#define __LIB_BISTREAM

#include "stream.h"

typedef struct {
    Stream *s;

    byte currentByte;
    byte bitOffset;

    int bytesConsumed;
} BitStream;

typedef struct {
    bit value;
    bool error;
} MaybeBit;

#define mkBitStream(stream) ((BitStream){ .s = stream, .currentByte = 0, .bitOffset = 255, .bytesConsumed = 0 })

bool bitstream_fill(BitStream *bs) {
    if(bs->bitOffset <= 7) return true;
    MaybeChar byte = stream_popChar(bs->s);
    if(isNone(byte)) return false;
    bs->currentByte = byte.value;
    bs->bitOffset = 0;
    bs->bytesConsumed += 1;
    return true;
}

MaybeBit bitstream_peek(BitStream *bs) {
    bool hasValue = bitstream_fill(bs);
    if(!hasValue) return none(MaybeBit);
    return just(MaybeBit, (bs->currentByte >> bs->bitOffset) & 1);
}

MaybeBit bitstream_pop(BitStream *bs) {
    MaybeBit value = bitstream_peek(bs);
    if(isNone(value)) return value;
    bs->bitOffset++;
    return value;
}

void bitstream_discard(BitStream *bs) {
    bs->bitOffset = 255;
    bs->currentByte = 0;
}

bool bitstream_flush(BitStream *bs) {
    bool result = stream_writeChar(bs->s, bs->currentByte);
    bs->currentByte = 0;
    bs->bitOffset = 0;
    return result;
}

bool bitstream_write(BitStream *bs, bit b) {
    if(bs->bitOffset > 8) {
        bs->currentByte = 0;
        bs->bitOffset = 0;
    }

    if(bs->bitOffset == 8) {
        bool result = bitstream_flush(bs);
        if(!result) return result;
    }

    bs->currentByte = bs->currentByte | ((b & 1) << bs->bitOffset);
    bs->bitOffset++;
    return true;
}

bool bitstream_writeN(BitStream *bs, u64 val, u8 len) {
    if(len == 0) return true;
    if(len > 64) return false;

    for(int i = 0; i < len; i++) {
        bool result = bitstream_write(bs, (val >> i) & 1);
        if(!result) return false;
    }

    return true;
}

#endif // __LIB_BISTREAM
