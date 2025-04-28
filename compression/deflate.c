#ifndef __LIB_DEFLATE
#define __LIB_DEFLATE

#include "types.h"
#include "mem.h"
#include "alloc.h"
#include "stream.h"
#include "dynar.h"

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
    MaybeChar byte = stream_popChar(bs->s);
    if(isNone(byte)) return false;
    bs->currentByte = byte.value;
    bs->bitOffset = 0;
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

#define DEFLATE_BLOCK_NON_COMPRESSED 0b00
#define DEFLATE_BLOCK_FIXED_HUFFMAN 0b01
#define DEFLATE_BLOCK_DYNAMIC_HUFFMAN 0b10

typedef struct {
    u32 code;
    u8 codeLen;

    byte value;
    bool isDist;
} DeflateDeCompElement;

typedef struct {
    bool error;

    DeflateDeCompElement *list;
    usz len;
} DeflateDeCompTable;

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

u16 DeflateLinLenValues[30] = {
    0,
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13,
    15, 17, 19, 23, 27, 31, 35, 43, 51, 59,
    67, 83, 99, 115, 131, 163, 195, 227, 258
};

u8 DeflateDistExtraBits[30] = {
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
    13, 13
};

u16 DeflateDistValues[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25,
    33, 49, 65, 97, 129, 193, 257, 385, 513, 769,
    1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577
};

// TODO: precalculate the entire structure

u8 DeflateLitLenLengths[288] = {
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,

    9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
    9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
    9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
    9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,

    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,

    8, 8, 8, 8, 8, 8, 8, 8
};
u8 DeflateDistLengths[32] = {
    5, 5, 5, 5, 5, 5, 5, 5,
    5, 5, 5, 5, 5, 5, 5, 5,
    5, 5, 5, 5, 5, 5, 5, 5,
    5, 5, 5, 5, 5, 5, 5, 5
};

void Deflate_sortDeCompTable(DeflateDeCompTable table) {
    // TODO: quicksort (too lazy now)
    for(int i = 0; i < table.len - 1; i++) {
        for(int j = 0; j < table.len - 1 - i; j++) {
            if(table.list[j].code > table.list[j + 1].code) {
                DeflateDeCompElement e = table.list[j];
                table.list[j] = table.list[j + 1];
                table.list[j + 1] = e;
            }
        }
    }
}

DeflateDeCompTable Deflate_generateDeCompTable(Dynar(u8) lengths, Alloc *alloc) {
    u16 blCount[32] = {0};
    for(int i = 0; i < lengths.len; i++) {
        u8 len = dynar_index(u8, lengths, i);
        if(len >= 32) return none(DeflateDeCompTable);
        blCount[len]++;
    }

    u16 nextCode[32] = {0};
    u16 code = 0;
    blCount[0] = 0;
    for(int i = 1; i < 32; i++) {
        code = (code + blCount[i - 1]) << 1;
        nextCode[i] = code;
    }

    DeflateDeCompTable table = {
        .list = (void *)AllocateBytesC(alloc, sizeof(DeflateDeCompElement) * lengths.len).s,
        .len = lengths.len
    };

    u16 value = 0;
    bool dist = false;
    for(int i = 0; i < table.len; i++) {
        u8 len = dynar_index(u8, lengths, i);
        table.list[i] = (DeflateDeCompElement){
            .code = nextCode[len] << (32 - len),
            .codeLen = len,
            .value = value,
            .isDist = dist
        };
        nextCode[len]++;

        if(value == 255) {
            dist = true;
            value = 0;
        }
        else {
            value++;
        }
    }

    Deflate_sortDeCompTable(table);
    return table;
}

bool Deflate_decompress_block_huffman(
    BitStream *in,
    Stream *out, StringBuilder *sbout,
    DeflateDeCompTable *litlen, DeflateDeCompTable *dist)
{
    while(true) {
        u32 code = 0;
        u8 offset = 31; // NOTE: because code is u32, and 1 << 31 sets the max bit
        u8 codeLen = 0;
        usz index = 0;

        while(true) {
            MaybeBit b = bitstream_pop(in);
            if(isNone(b)) return false;

            code = code | (b.value << offset);
            codeLen++;
            offset--;

            while(index < litlen->len &&
                    (code > litlen->list[index].code
                     )) {
                index++;
            }
            if(index >= litlen->len) return false;

            if(litlen->list[index].code == code && litlen->list[index].codeLen == codeLen) {
                // found the code
                break;
            }
        }

        DeflateDeCompElement litlenEntry = litlen->list[index];
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
            u8 distOffset = 31;
            u8 distCodeLen = 0;
            usz distIndex = 0;

            while(true) {
                MaybeBit b = bitstream_pop(in);
                if(isNone(b)) return false;

                distCode = distCode | (b.value << distOffset);
                distCodeLen++;
                distOffset--;

                while(distIndex < dist->len &&
                        (distCode > dist->list[distIndex].code
                         )) {
                    distIndex++;
                }
                if(distIndex >= dist->len) return false;

                if(dist->list[distIndex].code == distCode && dist->list[distIndex].codeLen == distCodeLen) {
                    // found the dist
                    break;
                }
            }

            DeflateDeCompElement distEntry = dist->list[distIndex];

            u8 extraDistBits = DeflateDistExtraBits[distEntry.value];
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
                bool result = sb_appendChar(sbout, sbout->s.s[i]);
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
    ResultRead result;

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
            Dynar(u8) litlenDynar = mkDynarML(u8, mkMem(DeflateLitLenLengths, 288), 288);
            Dynar(u8) distDynar = mkDynarML(u8, mkMem(DeflateDistLengths, 32), 32);

            DeflateDeCompTable litlen = Deflate_generateDeCompTable(litlenDynar, ALLOC);
            DeflateDeCompTable dist = Deflate_generateDeCompTable(distDynar, ALLOC);

            bool result = Deflate_decompress_block_huffman(&in, &out, &sb, &litlen, &dist);
            if(!result) return memnull;
        }
        else if(blockType == DEFLATE_BLOCK_DYNAMIC_HUFFMAN) {
            u16 hlit = 0;
            for(int i = 0; i < 5; i++) {
                MaybeBit b = bitstream_pop(&in);
                if(isNone(b)) return memnull;
                hlit = (hlit << 1) | b.value;
            }
            hlit += 257; 

            u8 hdist = 0;
            for(int i = 0; i < 5; i++) {
                MaybeBit b = bitstream_pop(&in);
                if(isNone(b)) return memnull;
                hdist = (hdist << 1) | b.value;
            }
            hdist += 1;

            u8 hclen = 0;
            for(int i = 0; i < 4; i++) {
                MaybeBit b = bitstream_pop(&in);
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

    return sb_build(sb);
}

Mem Deflate_compress(Mem raw) {
    return memnull;
}

#endif // __LIB_DEFLATE
