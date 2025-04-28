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

typedef struct {
    u32 code;
    u8 codeLen;

    byte value;
    bool isDist;
} DeflateDeCompLitLenElement;

typedef struct {
    DeflateDeCompLitLenElement *list;
    usz len;
} DeflateDeCompLitLen;

u8 DeflateLitLenExtraBits[30] = {
    0,
    0, 0, 0, 0, 0, 0, 0, 0,
    1, 1, 1, 1,
    2, 2, 2, 2,
    3, 3, 3, 3,
    4, 4, 4, 4,
    5, 5, 5, 5,
    0
};

u16 DeflateLinLenValues[30] {
    0,
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13,
    15, 17, 19, 23, 27, 31, 35, 43, 51, 59,
    67, 83, 99, 115, 131, 162, 163, 194, 195, 227, 258
};

u8 DeflateDistExtraBits[30] {
    0, 0, 0, 0,
    1, 1,
    2, 2,
    3, 3,
    4, 4,
    5, 5,
    6, 6,
    7, 7,
    8, 8,
    9, 9,
    10, 10,
    11, 11,
    12, 12,
    13, 13,
    0
};

u16 DeflateDistValues[30] {
    1, 2, 3, 4, 5, 7, 9, 13, 16, 17, 25,
    33, 49, 65, 97, 129, 193, 257, 385, 513, 769,
    1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577
};

typedef struct {
    u32 code;
    u8 codeLen;

    u8 index;
} DeflateDeCompDistElement;

typedef struct {
    DeflateDeCompDistElement *list;
    usz len;
} DeflateDeCompDist;

bool Deflate_decompress_block_huffman(
    BitStream *in,
    Stream *out, StringBuilder *sbout,
    DeflateDeCompLitLen *litlen, DeflateDeCompDist *dist)
{
    while(true) {
        u32 code = 0;
        u8 offset = 31; // NOTE: because code is u32, and 1 << 31 sets the max bit
        u8 codeLen = 0;
        usz index = 0;

        while(true) {
            MaybeBit b = bitstream_pop();
            if(isNone(b)) return false;

            code = code | (b.value << offset);
            codeLen++;
            offset--;

            while(index < litlen->len &&
                    (litlen->list[index].code < code
                     || litlen->list[index].codeLen != codeLen)) {
                index++;
            }
            if(index >= litlen->len) return false;

            if(litlen->list[index].code == code && litlen->list[index].codeLen == codeLen) {
                // found the code
                break;
            }
        }

        DeflateDeCompLitLenElement litlenEntry = litlen->list[index];
        if(litlenEntry.isDist && litlenEntry.value == 0) {
            // 256 = end of the block code
            return true;
        }
        else if(litlenEntry.isDist) {
            u8 extraBits = DeflateLitLenExtraBits[litlenEntry.value];
            u16 extraLen = 0;
            for(int i = extraBits; i > 0; i--) {
                MaybeBit b = bitstream_pop(in);
                if(isNone(b)) return false;
                extraLen = (extraLen << 1) | b.value;
            }
            u16 len = DeflateLinLenValues[litlenEntry.value] + extraLen;

            u32 distCode = 0;
            u8 distOffset = 0;
            u8 distCodeLen = 31;
            usz distIndex = 0;

            while(true) {
                MaybeBit b = bitstream_pop();
                if(isNone(b)) return false;

                distCode = distCode | (b.value << distOffset);
                distCodeLen++;
                distOffset--;

                while(distIndex < dist->len &&
                        (dist->list[distIndex].code < distCode
                         || dist->list[distIndex].codeLen != distCodeLen)) {
                    distIndex++;
                }
                if(distIndex >= dist->len) return false;

                if(dist->list[distIndex].code == distCode && dist->list[distIndex].codeLen == distCodeLen) {
                    // found the dist
                    break;
                }
            }

            DeflateDeCompDistElement distEntry = dist->list[distIndex];

            u8 extraDistBits = DeflateDistValues[distEntry.value];
            u16 extraDist = 0;
            for(int i = extraDistBits; i > 0; i--) {
                MaybeBit b = bitstream_pop(in);
                if(isNone(b)) return false;
                extraDist = (extraDist << 1) | b.value;
            }
            u16 fdist = DeflateDistValues[distEntry.value] + extraDist;
            if(fdist > sbout->len) return false;

            usz origin = (sbout->len - fdist);
            for(int i = origin; i < origin + len; i++) {
                bool result = sb_appendChar(sbout, sb->s[i]);
                if(!result) return false;
            }
        }
        else {
            bool result = stream_writeChar(out, litlenEntry.value);
            if(!result) return false;
        }
    }
}

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
            u16 hlit = 0;
            for(int i = 0; i < 5; i++) {
                MaybeBit b = bitstream_pop(in);
                if(isNone(b)) return memnull;
                hlit = (hlit << 1) | b.value;
            }
            hlit += 257; 

            u8 hdist = 0;
            for(int i = 0; i < 5; i++) {
                MaybeBit b = bitstream_pop(in);
                if(isNone(b)) return memnull;
                hdist = (hdist << 1) | b.value;
            }
            hdist += 1;

            u8 hclen = 0;
            for(int i = 0; i < 4; i++) {
                MaybeBit b = bitstream_pop(in);
                if(isNone(b)) return memnull;
                hclen = (hclen << 1) | b.value;
            }
            hclen += 4;

            // TODO: parse the lengths

            // TODO: call Deflate_decompress_block_huffman
        }
        else {
            return memnull;
        }
    } while(!finalBlock);
}

Mem Deflate_compress(Mem raw) {

}

#endif // __LIB_DEFLATE
