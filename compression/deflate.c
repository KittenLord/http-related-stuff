#ifndef __LIB_DEFLATE
#define __LIB_DEFLATE

// TODO: move to bitstream.h

typedef struct {
    Stream *s;

    byte currentByte;
    byte bitOffset;
} BitStream;

typedef struct {
    bit value;
    bool error;
} MaybeBit;

#define mkBitStream(stream) ((BitStream){ .s = stream, .currentByte = 0, .bitOffset = 255 })

bool bitstream_fill(BitStream *bs) {
    if(bs->bitOffset <= 7) return true;
    ReadResult result = stream_read(bs->s, &bs->currentByte);
    if(result.error || result.partial) return false;
    bs->bitOffset = 0;
    return true;
}

MaybeBit bitstream_peek(BitStream *bs) {
    bool hasValue = bitstream_fill(bs);
    if(!hasValue) return none(MaybeBit);
    return just((bs->currentByte >> bs->bitOffset) & 1);
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

#define DEFLATE_BLOCK_NON_COMPRESSED 0b00
#define DEFLATE_BLOCK_FIXED_HUFFMAN 0b01
#define DEFLATE_BLOCK_DYNAMIC_HUFFMAN 0b10

bool Deflate_decompress_block_noncomp(Stream *in, Stream *out, Alloc *alloc) {
    u16 len = 0;
    u16 nlen = 0;
    Mem m;
    ReadResult result;

    m = mkMem(&len, sizeof(u16));
    result = stream_read(in, m);
    if(result.partial || result.error) return false;

    m = mkMem(&nlen, sizeof(u16));
    result = stream_read(in, m);
    if(result.partial || result.error) return false;

    if(len != ~nlen) return false;

    // NOTE: use a variation of stream_route maybe?
    m = AllocateBytesC(alloc, len);
    result = stream_read(in, m);
    if(result.partial || result.error) return false;

    stream_write(out, m);
    FreeC(alloc, m.s);
    return true;
}

Mem Deflate_decompress(Mem raw, Alloc *alloc) {
    Stream inByte = mkStreamStr(raw);
    BitStream in = mkBitStream(&inByte);

    StringBuilder sb = mkStringBuilder();
    sb.alloc = alloc;
    Stream out = mkStreamSb(&sb);

    bool finalBlock = false;
    do {
        MaybeBit final = bitstream_pop(&in);
        if(isNone(final)) return memnull;
        if(final.value) finalBlock = true;

        MaybeBit a = bitstream_pop(&in);
        MaybeBit b = bitstream_pop(&in);
        if(isNone(a) || isNone(b)) return memnull; // shouldn't ever happen
        
        byte blockType = a.value | (b.value << 1);
        if(blockType == DEFLATE_BLOCK_NON_COMPRESSED) {
            bitstream_discard(&in);
            bool result = Deflate_decompress_block_noncomp(in.s, &out, ALLOC);
            if(!result) return memnull;
        }
        else if(blockType == DEFLATE_BLOCK_FIXED_HUFFMAN) {

        }
        else if(blockType == DEFLATE_BLOCK_DYNAMIC_HUFFMAN) {

        }
        else {
            return memnull;
        }
    } while(!finalBlock);
}

Mem Deflate_compress(Mem raw) {

}

#endif // __LIB_DEFLATE
