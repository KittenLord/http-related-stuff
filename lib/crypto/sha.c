#ifndef __LIB_SHA2
#define __LIB_SHA2

#include <types.h>
#include <mem.h>
#include <stream.h>
#include <alloc.h>

// Based on FIPS PUB 180-4
// https://nvlpubs.nist.gov/nistpubs/FIPS/NIST.FIPS.180-4.pdf

// TODO: it's a good idea to make this work with Stream rather than Mem

// TODO: iirc AMD64 has this natively, look into that
#define Sha_rotl(x, n, w) (((x) << (n)) | ((x) >> ((w) - (n))))
#define Sha_rotl32(x, n) (Sha_rotl(x, n, 32))
#define Sha_rotl64(x, n) (Sha_rotl(x, n, 64))
#define Sha_rotr(x, n, w) (((x) >> (n)) | ((x) << ((w) - (n))))
#define Sha_rotr32(x, n) (Sha_rotr(x, n, 32))
#define Sha_rotr64(x, n) (Sha_rotr(x, n, 64))
#define Sha_shr(x, n) ((x) >> (n))
#define Sha_shr32(x, n) (Sha_shr(x, n))
#define Sha_shr64(x, n) (Sha_shr(x, n))

#define Sha_Ch(x, y, z) (((x) & (y)) ^ ((~(x)) & (z)))
#define Sha_Parity(x, y, z) ((x) ^ (y) ^ (z))
#define Sha_Maj(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))

#define Sha_ft(t, x, y, z) ( \
    (0 <= (t) && (t) <= 19) ? Sha_Ch(x, y, z) : \
    (20 <= (t) && (t) <= 39) ? Sha_Parity(x, y, z) : \
    (40 <= (t) && (t) <= 59) ? Sha_Maj(x, y, z) : \
    (60 <= (t) && (t) <= 79) ? Sha_Parity(x, y, z) : \
    0)

#define Sha_CapSigma256_0(x) (Sha_rotr32(x, 2) ^ Sha_rotr32(x, 13) ^ Sha_rotr32(x, 22))
#define Sha_CapSigma256_1(x) (Sha_rotr32(x, 6) ^ Sha_rotr32(x, 11) ^ Sha_rotr32(x, 25))
#define Sha_Sigma256_0(x) (Sha_rotr32(x, 7) ^ Sha_rotr32(x, 18) ^ Sha_shr32(x, 3))
#define Sha_Sigma256_1(x) (Sha_rotr32(x, 17) ^ Sha_rotr32(x, 19) ^ Sha_shr32(x, 10))

#define Sha_CapSigma512_0(x) (Sha_rotr64(x, 28) ^ Sha_rotr64(x, 34) ^ Sha_rotr64(x, 39))
#define Sha_CapSigma512_1(x) (Sha_rotr64(x, 14) ^ Sha_rotr64(x, 18) ^ Sha_rotr64(x, 41))
#define Sha_Sigma512_0(x) (Sha_rotr64(x, 1) ^ Sha_rotr64(x, 8) ^ Sha_shr64(x, 7))
#define Sha_Sigma512_1(x) (Sha_rotr64(x, 19) ^ Sha_rotr64(x, 61) ^ Sha_shr64(x, 6))

#define Sha_K0 ((u32)0x5a827999)
#define Sha_K1 ((u32)0x6ed9eba1)
#define Sha_K2 ((u32)0x8f1bbcdc)
#define Sha_K3 ((u32)0xca62c1d6)

u32 Sha_K[80] = {
    Sha_K0, Sha_K0, Sha_K0, Sha_K0, Sha_K0,
    Sha_K0, Sha_K0, Sha_K0, Sha_K0, Sha_K0,
    Sha_K0, Sha_K0, Sha_K0, Sha_K0, Sha_K0,
    Sha_K0, Sha_K0, Sha_K0, Sha_K0, Sha_K0,

    Sha_K1, Sha_K1, Sha_K1, Sha_K1, Sha_K1,
    Sha_K1, Sha_K1, Sha_K1, Sha_K1, Sha_K1,
    Sha_K1, Sha_K1, Sha_K1, Sha_K1, Sha_K1,
    Sha_K1, Sha_K1, Sha_K1, Sha_K1, Sha_K1,

    Sha_K2, Sha_K2, Sha_K2, Sha_K2, Sha_K2,
    Sha_K2, Sha_K2, Sha_K2, Sha_K2, Sha_K2,
    Sha_K2, Sha_K2, Sha_K2, Sha_K2, Sha_K2,
    Sha_K2, Sha_K2, Sha_K2, Sha_K2, Sha_K2,

    Sha_K3, Sha_K3, Sha_K3, Sha_K3, Sha_K3,
    Sha_K3, Sha_K3, Sha_K3, Sha_K3, Sha_K3,
    Sha_K3, Sha_K3, Sha_K3, Sha_K3, Sha_K3,
    Sha_K3, Sha_K3, Sha_K3, Sha_K3, Sha_K3,
};

u32 Sha_K256[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

u64 Sha_K512[80] = {
    0x428a2f98d728ae22, 0x7137449123ef65cd, 0xb5c0fbcfec4d3b2f, 0xe9b5dba58189dbbc,
    0x3956c25bf348b538, 0x59f111f1b605d019, 0x923f82a4af194f9b, 0xab1c5ed5da6d8118,
    0xd807aa98a3030242, 0x12835b0145706fbe, 0x243185be4ee4b28c, 0x550c7dc3d5ffb4e2,
    0x72be5d74f27b896f, 0x80deb1fe3b1696b1, 0x9bdc06a725c71235, 0xc19bf174cf692694,
    0xe49b69c19ef14ad2, 0xefbe4786384f25e3, 0x0fc19dc68b8cd5b5, 0x240ca1cc77ac9c65,
    0x2de92c6f592b0275, 0x4a7484aa6ea6e483, 0x5cb0a9dcbd41fbd4, 0x76f988da831153b5,
    0x983e5152ee66dfab, 0xa831c66d2db43210, 0xb00327c898fb213f, 0xbf597fc7beef0ee4,
    0xc6e00bf33da88fc2, 0xd5a79147930aa725, 0x06ca6351e003826f, 0x142929670a0e6e70,
    0x27b70a8546d22ffc, 0x2e1b21385c26c926, 0x4d2c6dfc5ac42aed, 0x53380d139d95b3df,
    0x650a73548baf63de, 0x766a0abb3c77b2a8, 0x81c2c92e47edaee6, 0x92722c851482353b,
    0xa2bfe8a14cf10364, 0xa81a664bbc423001, 0xc24b8b70d0f89791, 0xc76c51a30654be30,
    0xd192e819d6ef5218, 0xd69906245565a910, 0xf40e35855771202a, 0x106aa07032bbd1b8,
    0x19a4c116b8d2d0c8, 0x1e376c085141ab53, 0x2748774cdf8eeb99, 0x34b0bcb5e19b48a8,
    0x391c0cb3c5c95a63, 0x4ed8aa4ae3418acb, 0x5b9cca4f7763e373, 0x682e6ff3d6b2b8a3,
    0x748f82ee5defb2fc, 0x78a5636f43172f60, 0x84c87814a1f0ab72, 0x8cc702081a6439ec,
    0x90befffa23631e28, 0xa4506cebde82bde9, 0xbef9a3f7b2c67915, 0xc67178f2e372532b,
    0xca273eceea26619c, 0xd186b8c721c0c207, 0xeada7dd6cde0eb1e, 0xf57d4f7fee6ed178,
    0x06f067aa72176fba, 0x0a637dc5a2c898a6, 0x113f9804bef90dae, 0x1b710b35131c471b,
    0x28db77f523047d84, 0x32caab7b40c72493, 0x3c9ebe0a15c9bebc, 0x431d67c49c100d4c,
    0x4cc5d4becb3e42b6, 0x597f299cfc657e2a, 0x5fcb6fab3ad6faec, 0x6c44198c4a475817,
};

typedef struct {
    union {
        byte data[20];
        u32 words[5];
    };
} Hash160;

typedef struct {
    union {
        byte data[32];
        u32 words[8];
    };
} Hash256;

typedef struct {
    union {
        byte data[28];
        u32 words[7];
    };
} Hash224;

typedef struct {
    byte data[64];
} Hash512;

typedef struct {
    union {
        byte data[64];
        u32 words[16];
    };
} Sha_Block512;

#define Sha_endian64(x) ( \
    ((x & 0x00000000000000ff) << (8*7)) | \
    ((x & 0x000000000000ff00) << (8*5)) | \
    ((x & 0x0000000000ff0000) << (8*3)) | \
    ((x & 0x00000000ff000000) << (8*1)) | \
    ((x & 0x000000ff00000000) >> (8*1)) | \
    ((x & 0x0000ff0000000000) >> (8*3)) | \
    ((x & 0x00ff000000000000) >> (8*5)) | \
    ((x & 0xff00000000000000) >> (8*7)))

#define Sha_endian32(x) ( \
    ((x & 0x000000ff) << (8*3)) | \
    ((x & 0x0000ff00) << (8*1)) | \
    ((x & 0x00ff0000) >> (8*1)) | \
    ((x & 0xff000000) >> (8*3)))

Sha_Block512 Sha_getBlock512(Mem mem, u64 totalLen, bool *writtenOne) {
    Sha_Block512 result = {0};
    mem_copy(mkMem(result.data, 64), mem);
    if(mem.len >= 64) {
        return result;
    }

    if(!(*writtenOne)) {
        result.data[mem.len] = 0b10000000;
        *writtenOne = true;
    }

    u64 filledBytes = mem.len + 1;
    if(64 - filledBytes >= 8) {
        u64 *len = (u64 *)((byte *)result.data + 64 - 8);
        *len = Sha_endian64(totalLen);
    }

    return result;
}

Hash160 Sha1(Mem mem) {
    // TODO: check max size

    u64 len = mem.len;

    Hash160 result = {0};
    result.words[0] = 0x67452301;
    result.words[1] = 0xefcdab89;
    result.words[2] = 0x98badcfe;
    result.words[3] = 0x10325476;
    result.words[4] = 0xc3d2e1f0;

    u64 blockCount = (mem.len / 64); // count full blocks
    u64 remainder = mem.len % 64;
    if(remainder != 0) { blockCount += 1; } // count partial block
    if(remainder == 0 || remainder >= 64 - 9) { blockCount += 1; } // count space for length
    bool writtenOne = false;

    for(u64 i = 1; i <= blockCount; i++) {
        Sha_Block512 block = Sha_getBlock512(mem, len * 8, &writtenOne);
        mem = memIndex(mem, 64);


        u32 W[80] = {0};
        for(int t = 0; t < 80; t++) {
            if(t <= 15) {
                W[t] = Sha_endian32(block.words[t]);
            }
            else {
                W[t] = Sha_rotl32(W[t - 3] ^ W[t - 8] ^ W[t - 14] ^ W[t - 16], 1);
            }
        }

        u32 a = result.words[0];
        u32 b = result.words[1];
        u32 c = result.words[2];
        u32 d = result.words[3];
        u32 e = result.words[4];

        for(int t = 0; t < 80; t++) {
            u32 T = Sha_rotl32(a, 5) + Sha_ft(t, b, c, d) + e + Sha_K[t] + W[t];
            e = d;
            d = c;
            c = Sha_rotl32(b, 30);
            b = a;
            a = T;
        }

        result.words[0] = a + result.words[0];
        result.words[1] = b + result.words[1];
        result.words[2] = c + result.words[2];
        result.words[3] = d + result.words[3];
        result.words[4] = e + result.words[4];
    }

    result.words[0] = Sha_endian32(result.words[0]);
    result.words[1] = Sha_endian32(result.words[1]);
    result.words[2] = Sha_endian32(result.words[2]);
    result.words[3] = Sha_endian32(result.words[3]);
    result.words[4] = Sha_endian32(result.words[4]);

    return result;
}

Hash256 Sha_Sha256Base(Mem mem, u32 initial[8]) {
    u64 len = mem.len;
    Hash256 result = {0};

    result.words[0] = initial[0];
    result.words[1] = initial[1];
    result.words[2] = initial[2];
    result.words[3] = initial[3];
    result.words[4] = initial[4];
    result.words[5] = initial[5];
    result.words[6] = initial[6];
    result.words[7] = initial[7];

    u64 blockCount = (mem.len / 64); // count full blocks
    u64 remainder = mem.len % 64;
    if(remainder != 0) { blockCount += 1; } // count partial block
    if(remainder == 0 || remainder >= 64 - 9) { blockCount += 1; } // count space for length
    bool writtenOne = false;

    for(u64 i = 1; i <= blockCount; i++) {
        Sha_Block512 block = Sha_getBlock512(mem, len * 8, &writtenOne);
        mem = memIndex(mem, 64);

        u32 W[64] = {0};
        for(int t = 0; t < 64; t++) {
            if(t <= 15) {
                W[t] = Sha_endian32(block.words[t]);
            }
            else {
                W[t] = Sha_Sigma256_1(W[t - 2]) + W[t - 7] +
                       Sha_Sigma256_0(W[t - 15]) + W[t - 16];
            }
        }

        u32 a = result.words[0];
        u32 b = result.words[1];
        u32 c = result.words[2];
        u32 d = result.words[3];
        u32 e = result.words[4];
        u32 f = result.words[5];
        u32 g = result.words[6];
        u32 h = result.words[7];

        for(int t = 0; t < 64; t++) {
            u32 T1 = h + Sha_CapSigma256_1(e) + Sha_Ch(e, f, g) + Sha_K256[t] + W[t];
            u32 T2 = Sha_CapSigma256_0(a) + Sha_Maj(a, b, c);
            h = g;
            g = f;
            f = e;
            e = d + T1;
            d = c;
            c = b;
            b = a;
            a = T1 + T2;
        }

        result.words[0] = a + result.words[0];
        result.words[1] = b + result.words[1];
        result.words[2] = c + result.words[2];
        result.words[3] = d + result.words[3];
        result.words[4] = e + result.words[4];
        result.words[5] = f + result.words[5];
        result.words[6] = g + result.words[6];
        result.words[7] = h + result.words[7];
    }

    result.words[0] = Sha_endian32(result.words[0]);
    result.words[1] = Sha_endian32(result.words[1]);
    result.words[2] = Sha_endian32(result.words[2]);
    result.words[3] = Sha_endian32(result.words[3]);
    result.words[4] = Sha_endian32(result.words[4]);
    result.words[5] = Sha_endian32(result.words[5]);
    result.words[6] = Sha_endian32(result.words[6]);
    result.words[7] = Sha_endian32(result.words[7]);

    return result;
}

Hash256 Sha256(Mem mem) {
    u32 initial[8] = {
        0x6a09e667,
        0xbb67ae85,
        0x3c6ef372,
        0xa54ff53a,
        0x510e527f,
        0x9b05688c,
        0x1f83d9ab,
        0x5be0cd19,
    };
    return Sha_Sha256Base(mem, initial);
}

Hash224 Sha224(Mem mem) {
    u32 initial[8] = {
        0xc1059ed8,
        0x367cd507,
        0x3070dd17,
        0xf70e5939,
        0xffc00b31,
        0x68581511,
        0x64f98fa7,
        0xbefa4fa4,
    };
    Hash256 result256 = Sha_Sha256Base(mem, initial);
    Hash224 result = {0};
    result.words[0] = result256.words[0];
    result.words[1] = result256.words[1];
    result.words[2] = result256.words[2];
    result.words[3] = result256.words[3];
    result.words[4] = result256.words[4];
    result.words[5] = result256.words[5];
    result.words[6] = result256.words[6];
    return result;
}

#endif // __LIB_SHA2
