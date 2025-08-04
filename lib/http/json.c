#ifndef __LIB_JSON
#define __LIB_JSON

// Based on JSON spec
// https://www.json.org/json-en.html

// This implementation slightly deviates from the specification,
// because it supports trailing commas for objects and arrays

// TODO: add comment support
// TODO: fix number parsing and serializing
// TODO: test this bad boy

// TODO: replace JSON with Json

#include <types.h>
#include <stream.h>
#include <dynar.h>

typedef enum {
    JSON_ERROR_SUCCESS,
    JSON_ERROR_NEVER,

    JSON_ERROR_EOF,
    JSON_ERROR_INVALID_CHARACTER,

    JSON_ERROR_NUMBER,
    JSON_ERROR_NUMBER_DECIMAL_POINT,
    JSON_ERROR_NUMBER_EXPONENT_SIGN,
    JSON_ERROR_NUMBER_EXPONENT_DIGITS,
    JSON_ERROR_STRING_CONTROL_CHARACTER,
    JSON_ERROR_STRING_NOT_TERMINATED,
    JSON_ERROR_OBJECT_EXPECTED_KEY,
    JSON_ERROR_OBJECT_EXPECTED_COLON,
    JSON_ERROR_UNEXPECTED_IDENTIFIER,
    JSON_ERROR_UNEXPECTED_CHARACTER,
} JsonErrorCode;

typedef struct {
    JsonErrorCode code;
    usz row;
    usz col;
} JsonError;

typedef struct JsonValue JsonValue;
typedef struct JsonKeyValue JsonKeyValue;

typedef struct {
    Dynar(JsonKeyValue) items;
} JsonObject;

typedef struct {
    Dynar(JsonValue) items;
} JsonArray;

typedef u8 JsonValueType;
#define JSON_NULL 0
#define JSON_STRING 1
#define JSON_NUMBER 2
#define JSON_OBJECT 3
#define JSON_ARRAY 4
#define JSON_BOOL 5
#define JSON_FLOAT 6
struct JsonValue {
    bool error;
    JsonError errmsg;

    JsonValueType type;
    union {
        String string;
        struct {
            i64 number;
            f64 fnumber;
        };
        JsonObject object;
        JsonArray array;
        bool boolean;
    };
};

struct JsonKeyValue {
    String key;
    JsonValue value;
};

#define mkJsonError(cd, s) ((JsonError){ .code = (cd), .row = (s)->row, .col = (s)->col })

bool JSON_isWhitespace(rune r) {
    return r == ' ' || r == '\t' || r == '\n' || r == '\r';
}

// TODO: figure out the extensive list of control characters that JSON disallows in the strings
bool JSON_isControl(rune r) {
    return r == '\n' && r == '\r';
}

// NOTE: JSON only supports 3 identifiers (true, false, null), neither of them have capital letters
bool JSON_isAlpha(rune r) {
    return (r >= 'a' && r <= 'z');
}

bool JSON_startNumber(rune r) {
    return r == '-' || (r >= '0' && r <= '9');
}

bool JSON_isDigit(rune r) {
    return r >= '0' && r <= '9';
}

MaybeRune JSON_popWhitespace(Stream *s) {
    MaybeRune r;
    while(!(r = stream_peekRune(s)).error && JSON_isWhitespace(r.value)) { 
        stream_popRune(s);
    }
    return r;
}

JsonValue JSON_parseValue(Stream *s, Alloc *alloc);

String JSON_parseIdentifier(Stream *s, Alloc *alloc) {
    StringBuilder sb = mkStringBuilderCap(16);
    sb.alloc = alloc;

    // NOTE: true, false, null are 5 characters at most
    for(int i = 0; i < 6; i++) {
        MaybeRune r = stream_peekRune(s);

        if(isNone(r)) {
            return sb_build(sb);
        }

        if(!JSON_isAlpha(r.value)) {
            return sb_build(sb);
        }

        sb_appendRune(&sb, r.value);
        stream_popRune(s);
    }

    String result = sb_build(sb);
    return result;
}

JsonValue JSON_parseNumber(Stream *s) {
    i64 number = 0;
    f64 fnumber = 0;
    bool sign;
    bool canBeInteger = true;

    MaybeRune r = stream_peekRune(s); // -, 0-9
    if(r.value == '-') {
        sign = true;
        stream_popRune(s);
        r = stream_peekRune(s);
    }

    if(isNone(r)) {
        return fail(JsonValue, mkJsonError(JSON_ERROR_NUMBER, s));
    }

    if(isJust(r) && r.value != '0') {
        while(isJust(r = stream_peekRune(s)) && JSON_isDigit(r.value)) {
            // TODO: i64 bounds checking

            stream_popRune(s); // pop the digit
            i64 digit = r.value - '0';
            if(number < 0) digit = -digit;
            number *= 10; number += digit;
            fnumber *= 10; fnumber += digit;

            if(sign) { number = -number; fnumber = -fnumber; sign = false; }
        }
    }
    else if(isJust(r) && r.value == '0') {
        stream_popRune(s); // pop zero
    }

    r = stream_peekRune(s);

    if(isFail(r, RUNE_EOF)) {
        JsonValueType type = canBeInteger ? JSON_NUMBER : JSON_FLOAT;
        return (JsonValue){ .type = type, .number = number, .fnumber = fnumber };
    }
    else if(isNone(r)) {
        return fail(JsonValue, mkJsonError(JSON_ERROR_NUMBER, s));
    }

    if(isJust(r) && r.value == '.') {
        stream_popRune(s);
        canBeInteger = false;
        bool atLeastOneDigit = false;
        f64 power = 10;

        while(isJust(r = stream_peekRune(s)) && JSON_isDigit(r.value)) {
            stream_popRune(s); // pop the digit
            atLeastOneDigit = true;
            f64 digit = (f64)(r.value - '0');
            digit /= power;
            power *= 10;
            fnumber += digit;
        }

        if(!atLeastOneDigit) {
            return fail(JsonValue, mkJsonError(JSON_ERROR_NUMBER_DECIMAL_POINT, s));
        }
    }

    r = stream_peekRune(s);

    if(isFail(r, RUNE_EOF)) {
        JsonValueType type = canBeInteger ? JSON_NUMBER : JSON_FLOAT;
        return (JsonValue){ .type = type, .number = number, .fnumber = fnumber };
    }
    else if(isNone(r)) {
        return fail(JsonValue, mkJsonError(JSON_ERROR_NUMBER, s));
    }

    if(isJust(r) && (r.value == 'e' || r.value == 'E')) {
        stream_popRune(s); // pop the exponent

        r = stream_peekRune(s);

        if(isNone(r) || (isJust(r) && r.value != '+' && r.value != '-')) {
            return fail(JsonValue, mkJsonError(JSON_ERROR_NUMBER_EXPONENT_SIGN, s));
        }

        r = stream_popRune(s);
        bool expNegative = r.value == '-';
        bool makeExpNegative = expNegative;
        bool atLeastOneDigit = false;
        f64 exponent = 0;

        while(isJust(r = stream_peekRune(s)) && JSON_isDigit(r.value)) {
            stream_popRune(s); // pop the digit
            atLeastOneDigit = true;
            i64 digit = r.value - '0';
            exponent *= 10;
            exponent += digit;
            if(makeExpNegative && digit != 0) { exponent = -exponent; makeExpNegative = false; }
        }

        if(!atLeastOneDigit) {
            return fail(JsonValue, mkJsonError(JSON_ERROR_NUMBER_EXPONENT_DIGITS, s));
        }

        for(f64 i = 0; i < exponent; i += 1) {
            if(expNegative) { number /= 10; fnumber /= 10; }
            else            { number *= 10; fnumber *= 10; }
        }
    }

    JsonValueType type = canBeInteger ? JSON_NUMBER : JSON_FLOAT;
    return (JsonValue){ .type = type, .number = number, .fnumber = fnumber };
}

JsonValue JSON_parseString(Stream *s, Alloc *alloc) {
    stream_popRune(s); // pop the double-quote

    StringBuilder sb = mkStringBuilder();
    sb.alloc = alloc;

    MaybeRune r;
    while(!(r = stream_peekRune(s)).error && !JSON_isControl(r.value) && r.value != '\"') {
        stream_popRune(s);
        if(r.value == '\\') {
            // TODO: escape sequences
            continue;
        }

        sb_appendRune(&sb, r.value);
    }

    if(isJust(r) && r.value == '\"') {
        stream_popRune(s);
        JsonValue result = { .type = JSON_STRING, .string = sb_build(sb) };
        return result;
    }

    if(isJust(r) && JSON_isControl(r.value)) {
        return fail(JsonValue, mkJsonError(JSON_ERROR_STRING_CONTROL_CHARACTER, s));
    }

    if(isNone(r)) {
        return fail(JsonValue, mkJsonError(JSON_ERROR_STRING_NOT_TERMINATED, s));
    }

    return fail(JsonValue, mkJsonError(JSON_ERROR_NEVER, s));
}

JsonValue JSON_parseObject(Stream *s, Alloc *alloc) {
    stream_popRune(s); // pop the opening curly brace

    JsonObject object = {0};
    object.items = mkDynarA(JsonKeyValue, alloc);

    MaybeRune r = JSON_popWhitespace(s);
    while(isJust(r) && r.value != '}') {

        if(isJust(r) && r.value != '\"') {
            return fail(JsonValue, mkJsonError(JSON_ERROR_OBJECT_EXPECTED_KEY, s));
        }

        JsonValue key = JSON_parseString(s, alloc);
        if(isNone(key)) return key;

        r = JSON_popWhitespace(s);

        if(isNone(r) || (isJust(r) && r.value != ':')) {
            return fail(JsonValue, mkJsonError(JSON_ERROR_OBJECT_EXPECTED_COLON, s));
        }

        stream_popRune(s); // pop the colon

        JsonValue value = JSON_parseValue(s, alloc);

        JsonKeyValue keyValue = { .key = key.string, .value = value };
        dynar_append(&object.items, JsonKeyValue, keyValue, _);

        r = stream_peekRune(s);

        if(isJust(r) && r.value == ',') {
            stream_popRune(s); // pop the comma

            r = JSON_popWhitespace(s);

            if(isJust(r) && r.value == '}') {
                break;
            }
        }
    }

    if(isNone(r)) {
        if(isFail(r, RUNE_EOF)) {
            return fail(JsonValue, mkJsonError(JSON_ERROR_EOF, s));
        }
        else {
            return fail(JsonValue, mkJsonError(JSON_ERROR_INVALID_CHARACTER, s));
        }
    }

    if(isJust(r) && r.value == '}') {
        stream_popRune(s);
    }

    JsonValue result = { .type = JSON_OBJECT, .object = object };
    return result;
}

JsonValue JSON_parseArray(Stream *s, Alloc *alloc) {
    stream_popRune(s); // pop the opening bracket

    JsonArray array = {0};
    array.items = mkDynarA(JsonValue, alloc);

    MaybeRune r = JSON_popWhitespace(s);
    if(isNone(r)) {
        if(isFail(r, RUNE_EOF)) {
            return fail(JsonValue, mkJsonError(JSON_ERROR_EOF, s));
        }
        else {
            return fail(JsonValue, mkJsonError(JSON_ERROR_INVALID_CHARACTER, s));
        }
    }
    while(isJust(r) && r.value != ']') {
        JsonValue value = JSON_parseValue(s, alloc);

        if(isNone(value)) {
            return value;
        }

        dynar_append(&array.items, JsonValue, value, _);
        
        r = stream_peekRune(s);

        if(isJust(r) && r.value == ',') {
            stream_popRune(s); // pop the comma

            r = JSON_popWhitespace(s);

            if(isJust(r) && r.value == ']') {
                break;
            }
        }
    }

    if(isNone(r)) {
        if(isFail(r, RUNE_EOF)) {
            return fail(JsonValue, mkJsonError(JSON_ERROR_EOF, s));
        }
        else {
            return fail(JsonValue, mkJsonError(JSON_ERROR_INVALID_CHARACTER, s));
        }
    }

    if(isJust(r) && r.value == ']') {
        stream_popRune(s);
    }

    JsonValue result = { .type = JSON_ARRAY, .array = array };
    return result;
}

JsonValue JSON_parseValue(Stream *s, Alloc *alloc) {
    MaybeRune r = JSON_popWhitespace(s);
    if(isNone(r)) {
        if(isFail(r, RUNE_EOF)) {
            return fail(JsonValue, mkJsonError(JSON_ERROR_EOF, s));
        }
        else {
            return fail(JsonValue, mkJsonError(JSON_ERROR_INVALID_CHARACTER, s));
        }
    }

    JsonValue result;
    String identifier;
    if(r.value == '\"') {
        result = JSON_parseString(s, alloc);
    }
    else if(r.value == '{') {
        result = JSON_parseObject(s, alloc);
        if(isNone(result)) return result;
    }
    else if(r.value == '[') {
        result = JSON_parseArray(s, alloc);
        if(isNone(result)) return result;
    }
    else if(r.value == 't' || r.value == 'f') {
        identifier = JSON_parseIdentifier(s, alloc);
        if(str_equal(identifier, mkString("true"))) {
            result = (JsonValue){ .type = JSON_BOOL, .boolean = true };
        }
        else if(str_equal(identifier, mkString("false"))) {
            result = (JsonValue){ .type = JSON_BOOL, .boolean = false };
        }
        else {
            return fail(JsonValue, mkJsonError(JSON_ERROR_UNEXPECTED_IDENTIFIER, s));
        }
    }
    else if(r.value == 'n') {
        identifier = JSON_parseIdentifier(s, alloc);
        if(str_equal(identifier, mkString("null"))) {
            result = (JsonValue){ .type = JSON_NULL };
        }
        else {
            return fail(JsonValue, mkJsonError(JSON_ERROR_UNEXPECTED_IDENTIFIER, s));
        }
    }
    else if(JSON_startNumber(r.value)) {
        result = JSON_parseNumber(s);
    }
    else {
        return fail(JsonValue, mkJsonError(JSON_ERROR_UNEXPECTED_CHARACTER, s));
    }

    JSON_popWhitespace(s);

    return result;
}

JsonValue JSON_parse(Stream *s, Alloc *alloc) {
    JsonValue value = JSON_parseValue(s, alloc);
    if(isNone(value)) return value;
    MaybeRune r = stream_peekRune(s);

    if(!isFail(r, RUNE_EOF)) {
        return fail(JsonValue, mkJsonError(JSON_ERROR_UNEXPECTED_CHARACTER, s));
    }

    return value;
}

bool JSON_serializeValue(JsonValue value, Stream *s, bool doIndent, usz indent);

bool JSON_serializeString(String string, Stream *s) {
    Stream str = mkStreamStr(string);
    if(!stream_writeRune(s, '\"')) return false;
    MaybeRune mr;
    bool result = true;
    // TODO: JSON may allow any byte sequence, gotta check that
    while(isJust(mr = stream_popRune(&str))) {
        rune r = mr.value;
        // TODO: add all escape characters from the spec
        if     (r == '\n') { checkRet(stream_writeRune(s, '\\')); checkRet(stream_writeRune(s, 'n')); }
        else if(r == '\"') { checkRet(stream_writeRune(s, '\\')); checkRet(stream_writeRune(s, '\"')); }
        else { checkRet(stream_writeRune(s, r)); }
    }

    checkRet(stream_writeRune(s, '\"'));

    return true;
}

bool JSON_serializeArray(JsonValue value, Stream *s, bool doIndent, usz indent) {
    if(value.type != JSON_ARRAY) return false;

    JsonArray array = value.array;

    checkRet(stream_writeRune(s, '['));

    if(array.items.len == 0) {
        checkRet(stream_writeRune(s, ']'));
        return true;
    }

    if(doIndent) checkRet(stream_writeRune(s, '\n'));

    dynar_foreach(JsonValue, &array.items) {
        for(usz i = 0; doIndent && i < indent + 4; i++) {
            checkRet(stream_writeRune(s, ' '));
        }
        checkRet(JSON_serializeValue(loop.it, s, doIndent, indent + 4));
        if(loop.index != array.items.len - 1) {
            checkRet(stream_writeRune(s, ','));
        }
        if(doIndent) {
            checkRet(stream_writeRune(s, '\n'));
        }
    }

    for(usz i = 0; doIndent && i < indent; i++) {
        checkRet(stream_writeRune(s, ' '));
    }

    checkRet(stream_writeRune(s, ']'));
    return true;
}

bool JSON_serializeObject(JsonValue value, Stream *s, bool doIndent, usz indent) {
    if(value.type != JSON_OBJECT) return false;

    JsonObject object = value.object;

    checkRet(stream_writeRune(s, '{'));

    if(object.items.len == 0) {
        checkRet(stream_writeRune(s, '}'));
        return true;
    }

    if(doIndent) checkRet(stream_writeRune(s, '\n'));

    dynar_foreach(JsonKeyValue, &object.items) {
        for(usz i = 0; doIndent && i < indent + 4; i++) {
            checkRet(stream_writeRune(s, ' '));
        }

        checkRet(JSON_serializeString(loop.it.key, s));

        if(doIndent) checkRet(stream_writeRune(s, ' '));
        checkRet(stream_writeRune(s, ':'));
        if(doIndent) checkRet(stream_writeRune(s, ' '));

        checkRet(JSON_serializeValue(loop.it.value, s, doIndent, indent + 4));

        if(loop.index != object.items.len - 1) checkRet(stream_writeRune(s, ','));

        if(doIndent) checkRet(stream_writeRune(s, '\n'));
        return true;
    }

    for(usz i = 0; doIndent && i < indent; i++) {
        checkRet(stream_writeRune(s, ' '));
    }

    checkRet(stream_writeRune(s, '}'));
    return true;
}

bool JSON_serializeNull(JsonValue value, Stream *s) {
    if(value.type != JSON_NULL) return false;

    checkRet(stream_writeRune(s, 'n'));
    checkRet(stream_writeRune(s, 'u'));
    checkRet(stream_writeRune(s, 'l'));
    checkRet(stream_writeRune(s, 'l'));

    return true;
}

bool JSON_serializeBool(JsonValue value, Stream *s) {
    if(value.type != JSON_BOOL) return false;

    if(value.boolean) {
        checkRet(stream_writeRune(s, 't'));
        checkRet(stream_writeRune(s, 'r'));
        checkRet(stream_writeRune(s, 'u'));
        checkRet(stream_writeRune(s, 'e'));
    }
    else {
        checkRet(stream_writeRune(s, 'f'));
        checkRet(stream_writeRune(s, 'a'));
        checkRet(stream_writeRune(s, 'l'));
        checkRet(stream_writeRune(s, 's'));
        checkRet(stream_writeRune(s, 'e'));
    }

    return true;
}

bool JSON_serializeNumber(JsonValue value, Stream *s) {
    if(value.type != JSON_NUMBER) return false;
    bool result = true;

    StringBuilder sb = mkStringBuilderCap(32);
    i64 number = value.number;
    bool sign = number < 0;

    while(number != 0) {
        i64 digit = number % 10;
        if(digit < 0) digit = -digit;
        sb_appendRune(&sb, (rune)(digit + '0'));
        number /= 10;
    }

    String str = sb_build(sb);
    if(sign) checkRet(stream_writeRune(s, '-'));
    for(int i = str.len - 1; i >= 0; i--) {
        checkRet(stream_writeRune(s, str.s[i]));
    }

    return true;
}

bool JSON_serializeValue(JsonValue value, Stream *s, bool doIndent, usz indent) {
    if(isNone(value)) return false;

    if(value.type == JSON_OBJECT)       return JSON_serializeObject(value, s, doIndent, indent);
    else if(value.type == JSON_STRING)  return JSON_serializeString(value.string, s);
    else if(value.type == JSON_ARRAY)   return JSON_serializeArray(value, s, doIndent, indent);
    else if(value.type == JSON_NULL)    return JSON_serializeNull(value, s);
    else if(value.type == JSON_BOOL)    return JSON_serializeBool(value, s);
    else if(value.type == JSON_NUMBER)  return JSON_serializeNumber(value, s);
    else { return false; }
}

bool JSON_serialize(JsonValue value, Stream *s, bool doIndent) {
    if(isNone(value)) return false;
    return JSON_serializeValue(value, s, doIndent, 0);
}

#define XRES ___jsonResult

#define mkJson(finalResult) \
JsonValue finalResult = {0}; \
for(JsonValue *XRES = &finalResult; XRES != null; XRES = null)

#define mkJsonObject \
*XRES = ((JsonValue){ .type = JSON_OBJECT, .object = ((JsonObject){ .items = mkDynar(JsonKeyValue) }) }); \
for(bool ___once = true; ___once; ___once = false)

#define mkJsonArray \
*XRES = ((JsonValue){ .type = JSON_ARRAY, .array = ((JsonArray){ .items = mkDynar(JsonValue) }) }); \
for(bool ___once = true; ___once; ___once = false)

#define mkJsonElement \
dynar_append(&XRES->array.items, JsonValue, ((JsonValue){}), _) \
for(JsonValue *___temp = XRES, *XRES = &dynar_peek(JsonValue, &___temp->array.items); XRES != null; XRES = null)

#define mkJsonKVL(s) \
dynar_append(&XRES->object.items, JsonKeyValue, ((JsonKeyValue){ .key = (s) }), _) \
for(JsonValue *___temp = XRES, *XRES = &dynar_peek(JsonKeyValue, &___temp->object.items).value; XRES != null; XRES = null)
#define mkJsonKV(s) mkJsonKVL(mkString(s))

#define mkJsonStringL(s) \
*XRES = ((JsonValue){ .type = JSON_STRING, .string = (s) });
#define mkJsonString(s) mkJsonStringL(mkString(s))

#define mkJsonNumber(n) \
*XRES = ((JsonValue){ .type = JSON_NUMBER, .number = (n), .fnumber = (n) });

#define mkJsonBool(b) \
*XRES = ((JsonValue){ .type = JSON_BOOL, .boolean = (b) });

#define mkJsonNull \
*XRES = ((JsonValue){ .type = JSON_NULL });

#undef XRES

#endif // __LIB_JSON
