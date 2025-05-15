#ifndef __LIB_TEXT
#define __LIB_TEXT

#include "types.h"
#include "str.h"
#include "stream.h"

// Based on RFC-4686 (base64 specifically, hex is obvious lol)
// https://datatracker.ietf.org/doc/html/rfc4648

u8 alphabet_hex[16] = {
    '0', '1', '2', '3', '4',
    '5', '6', '7', '8', '9',
    'a', 'b', 'c', 'd', 'e', 'f'
};

u8 alphabet_base64[64] = {
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J',
    'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T',
    'U', 'V', 'W', 'X', 'Y', 'Z',
    'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j',
    'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't',
    'u', 'v', 'w', 'x', 'y', 'z',
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
    '+', '/'
};

// TODO: maybe change u8 to rune?
u8 alphabet_english[26] = {
    'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j',
    'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't',
    'u', 'v', 'w', 'x', 'y', 'z',
};

bool isDigit(byte c) {
    return c >= '0' && c <= '9';
}

bool isHexDigit(byte c) {
    return (c >= '0' && c <= '9')
        || (c >= 'a' && c <= 'f')
        || (c >= 'A' && c <= 'F');
}

bool parseU8FromHexDigit(byte c, u8 *result) {
    if(!isHexDigit(c)) return false;
    if(result == null) return true;
    if(c >= '0' && c <= '9') { *result = c - '0'; }
    if(c >= 'a' && c <= 'f') { *result = c - 'a' + 10; }
    if(c >= 'A' && c <= 'F') { *result = c - 'A' + 10; }
    return true;
}

void toLower(String s) {
    for(usz i = 0; i < s.len; i++) {
        if(s.s[i] >= 'A' && s.s[i] <= 'Z') {
            s.s[i] = s.s[i] - 'A' + 'a';
        }
    }
}

bool writeBytesToHex(Stream *in, Stream *out, bool capital, bool prefix) {
    MaybeChar c;

    if(prefix) {
        pure(r) stream_writeChar(out, '0');
        cont(r) stream_writeChar(out, 'x');
        if(!r) return false;
    }

    while(isJust(c = stream_popChar(in))) {
        u8 lo = c.value & 0b1111;
        u8 hi = (c.value >> 4) & 0b1111;

        u8 loc = alphabet_hex[lo];
        u8 hic = alphabet_hex[hi];

        if(capital) {
            if(loc >= 'a' && loc <= 'f') loc = loc - 'a' + 'A';
            if(hic >= 'a' && hic <= 'f') hic = hic - 'a' + 'A';
        }

        pure(r) stream_writeChar(out, hic);
        cont(r) stream_writeChar(out, loc);
        if(!r) return false;
    }

    return true;
}

bool writeBytesToBase64(Stream *in, Stream *out, bool pad, bool urlVersion) {
    ResultRead result;
    u32 bufferBack = 0;
    while(isJust(result = stream_read(in, mkMem((byte *)&bufferBack, 3)))) {
        u8 len = result.read;
        if(len == 0) {
            return true;
        }

        for(int i = 0; i < len; i++) {
            u8 a = bufferBack & 0b111111;
            bufferBack >>= 6;
            u8 c = alphabet_base64[a];

            if(urlVersion) {
                if(c == '+') c = '-';
                else if(c == '/') c = '_';
            }

            if(!stream_writeChar(out, c)) return false;
        }

        if(pad) {
            for(int i = 0; i < 3 - len; i++) {
                if(!stream_writeChar(out, '=')) return false;
            }
        }
    }

    return true;
}

bool writeU64ToDecimal(Stream *out, u64 number) {
    if(number == 0) { return stream_writeChar(out, '0'); }
    u64 div = u64decmax;
    bool hasNonZero = false;

    while(div != 0) {
        u64 digit = number / div;
        number %= div;
        div /= 10;
        if(digit != 0) hasNonZero = true;

        if(digit == 0 && !hasNonZero) continue;
        if(!stream_writeChar(out, digit + '0')) return false;
    }

    return true;
}

bool parseU64FromDecimal(Stream *in, u64 *resultp, bool exhaust) {
    u64 acc = 0;
    MaybeChar c;
    u8 count = 0;
    while(isJust(c = stream_peekChar(in)) && isDigit(c.value)) {
        if(acc >= u64decmax) return false;
        acc = (acc * 10) + (c.value - '0');
        stream_popChar(in);
        count += 1;
    }

    if(count == 0) return false;

    *resultp = acc;
    if(exhaust && isJust(c)) { return false; }
    return true;
}

bool parseU64FromDecimalFixed(Stream *in, u64 *resultp, u8 digits, bool exhaust) {
    u64 result = 0;
    for(int i = 0; i < digits; i++) {
        MaybeChar c = stream_peekChar(in);
        if(isNone(c)) return false;
        if(!isDigit(c.value)) return false;
        stream_popChar(in);

        result = (result * 10) + (c.value - '0');
    }

    if(exhaust && isJust(stream_peekChar(in))) return false;

    *resultp = result;
    return true;
}

bool parseU64FromHex(Stream *in, u64 *resultp, bool exhaust) {
    u8 count = 0;
    u64 result = 0;
    MaybeChar c;
    while(isJust(c = stream_peekChar(in))) {
        if(count >= 16) return false; // 16 hex digits -> already 8 bytes
        u8 digit;
        if(!parseU8FromHexDigit(c.value, &digit)) break;
        stream_popChar(in);
        result = (result << 4) | digit;
        count += 1;
    }

    if(count == 0) return false;

    *resultp = result;
    if(exhaust && isJust(c)) return false;
    return true;
}

#endif // __LIB_TEXT
