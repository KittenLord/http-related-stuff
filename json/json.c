#include <stdio.h>

typedef struct JsonObject JsonObject;
typedef struct JsonArray JsonArray;
typedef struct JsonValue JsonValue;
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
    String errmsg;

    JsonValueType type;
    union {
        String string;
        struct {
            i64 number;
            f64 fnumber;
        };
        JsonObject *object;
        JsonArray *array;
        bool boolean;
    };
};

typedef struct JsonKeyValue JsonKeyValue;
struct JsonKeyValue {
    String key;
    JsonValue value;

    JsonKeyValue *next;
};

typedef struct JsonArrayElement JsonArrayElement;
struct JsonArrayElement {
    JsonValue value;

    JsonArrayElement *next;
};

struct JsonObject {
    JsonKeyValue *items;
    usz length;
};

struct JsonArray {
    JsonArrayElement *items;
    usz length;
};

bool JSON_isWhitespace(rune r);
bool JSON_isControl(rune r);
bool JSON_isAlpha(rune r);
bool JSON_startNumber(rune r);
bool JSON_isDigit(rune r);

MaybeRune JSON_popWhitespace(PeekStream *s);
String JSON_parseIdentifier(PeekStream *s, Alloc *alloc);

JsonValue JSON_parseNumber(PeekStream *s);
JsonValue JSON_parseObject(PeekStream *s, Alloc *alloc);
JsonValue JSON_parseArray(PeekStream *s, Alloc *alloc);
JsonValue JSON_parseString(PeekStream *s, Alloc *alloc);
JsonValue JSON_parseValue(PeekStream *s, Alloc *alloc);



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

MaybeRune JSON_popWhitespace(PeekStream *s) {
    MaybeRune r;
    while(!(r = pstream_peekRune(s)).error && JSON_isWhitespace(r.value)) { 
        pstream_popRune(s);
    }
    return r;
}

String JSON_parseIdentifier(PeekStream *s, Alloc *alloc) {
    StringBuilder sb = mkStringBuilderCap(16);
    sb.alloc = alloc;

    // NOTE: true, false, null are 5 characters at most
    for(int i = 0; i < 6; i++) {
        MaybeRune r = pstream_peekRune(s);

        if(isNone(r)) {
            return sb_build(sb);
        }

        if(!JSON_isAlpha(r.value)) {
            return sb_build(sb);
        }

        sb_appendRune(&sb, r.value);
        pstream_popRune(s);
    }

    String result = sb_build(sb);
    return result;
}

JsonValue JSON_parseNumber(PeekStream *s) {
    i64 number = 0;
    f64 fnumber = 0;
    bool sign;
    bool canBeInteger = true;

    MaybeRune r = pstream_peekRune(s); // -, 0-9
    if(r.value == '-') {
        sign = true;
        pstream_popRune(s);
        r = pstream_peekRune(s);
    }

    if(isNone(r)) {
        return fail(JsonValue, mkString("Couldn't read the full number" DEBUG_LOC));
    }

    if(isJust(r) && r.value != '0') {
        while(isJust(r = pstream_peekRune(s)) && JSON_isDigit(r.value)) {
            // TODO: i64 bounds checking

            pstream_popRune(s); // pop the digit
            i64 digit = r.value - '0';
            number *= 10; number += digit;
            fnumber *= 10; fnumber += digit;

            if(sign) { number = -number; fnumber = -fnumber; sign = false; }
        }
    }
    else if(isJust(r) && r.value == '0') {
        pstream_popRune(s); // pop zero
    }

    r = pstream_peekRune(s);

    if(isFail(r, RUNE_EOF)) {
        JsonValueType type = canBeInteger ? JSON_NUMBER : JSON_FLOAT;
        return (JsonValue){ .type = type, .number = number, .fnumber = fnumber };
    }
    else if(isNone(r)) {
        return fail(JsonValue, mkString("Invalid character" DEBUG_LOC));
    }

    if(isJust(r) && r.value == '.') {
        pstream_popRune(s);
        canBeInteger = false;
        bool atLeastOneDigit = false;
        f64 power = 10;

        while(isJust(r = pstream_peekRune(s)) && JSON_isDigit(r.value)) {
            pstream_popRune(s); // pop the digit
            atLeastOneDigit = true;
            f64 digit = (f64)(r.value - '0');
            digit /= power;
            power *= 10;
            fnumber += digit;
        }

        if(!atLeastOneDigit) {
            return fail(JsonValue, mkString("No digits after a decimal point" DEBUG_LOC));
        }
    }

    r = pstream_peekRune(s);

    if(isFail(r, RUNE_EOF)) {
        JsonValueType type = canBeInteger ? JSON_NUMBER : JSON_FLOAT;
        return (JsonValue){ .type = type, .number = number, .fnumber = fnumber };
    }
    else if(isNone(r)) {
        return fail(JsonValue, mkString("Invalid character" DEBUG_LOC));
    }

    if(isJust(r) && (r.value == 'e' || r.value == 'E')) {
        pstream_popRune(s); // pop the exponent

        r = pstream_peekRune(s);

        if(isNone(r) || (isJust(r) && r.value != '+' && r.value != '-')) {
            return fail(JsonValue, mkString("No sign after an exponent identifier" DEBUG_LOC));
        }

        r = pstream_popRune(s);
        bool expNegative = r.value == '-';
        bool makeExpNegative = expNegative;
        bool atLeastOneDigit = false;
        f64 exponent = 0;

        while(isJust(r = pstream_peekRune(s)) && JSON_isDigit(r.value)) {
            pstream_popRune(s); // pop the digit
            atLeastOneDigit = true;
            i64 digit = r.value - '0';
            exponent *= 10;
            exponent += digit;
            if(makeExpNegative && digit != 0) { exponent = -exponent; makeExpNegative = false; }
        }

        if(!atLeastOneDigit) {
            return fail(JsonValue, mkString("No digits after an exponent" DEBUG_LOC));
        }

        for(f64 i = 0; i < exponent; i += 1) {
            if(expNegative) { number /= 10; fnumber /= 10; }
            else            { number *= 10; fnumber *= 10; }
        }
    }

    JsonValueType type = canBeInteger ? JSON_NUMBER : JSON_FLOAT;
    return (JsonValue){ .type = type, .number = number, .fnumber = fnumber };
}

JsonValue JSON_parseObject(PeekStream *s, Alloc *alloc) {
    pstream_popRune(s); // pop the opening curly brace

    JsonObject object = {0};
    JsonKeyValue *last = null;

    MaybeRune r = JSON_popWhitespace(s);
    while(isJust(r) && r.value != '}') {

        if(isJust(r) && r.value != '\"') {
            return fail(JsonValue, mkString("Expected a key literal" DEBUG_LOC));
        }

        JsonValue key = JSON_parseString(s, alloc);
        if(isNone(key)) return key;

        r = JSON_popWhitespace(s);

        if(isNone(r) || (isJust(r) && r.value != ':')) {
            return fail(JsonValue, mkString("Expected a colon :" DEBUG_LOC));
        }

        pstream_popRune(s); // pop the colon

        JsonValue value = JSON_parseValue(s, alloc);

        JsonKeyValue keyValue = { .key = key.string, .value = value };
        JsonKeyValue *pkeyValue = alloc->alloc(*alloc, sizeof(JsonKeyValue));
        *pkeyValue = keyValue;

        object.length++;

        if(last == null) {
            object.items = pkeyValue;
            last = pkeyValue;
        }
        else {
            last->next = pkeyValue;
            last = pkeyValue;
        }

        r = pstream_peekRune(s);

        if(isJust(r) && r.value == ',') {
            pstream_popRune(s); // pop the comma

            r = JSON_popWhitespace(s);

            if(isJust(r) && r.value == '}') {
                break;
            }
        }
    }

    if(isNone(r)) {
        if(isFail(r, RUNE_EOF)) {
            return fail(JsonValue, mkString("End of file" DEBUG_LOC));
        }
        else {
            return fail(JsonValue, mkString("Invalid character" DEBUG_LOC));
        }
    }

    if(isJust(r) && r.value == '}') {
        pstream_popRune(s);
    }

    JsonObject *pobject = alloc->alloc(*alloc, sizeof(JsonObject));
    *pobject = object;
    JsonValue result = { .type = JSON_OBJECT, .object = pobject };
    return result;
}

JsonValue JSON_parseArray(PeekStream *s, Alloc *alloc) {
    pstream_popRune(s); // pop the opening bracket

    JsonArray array = {0};
    JsonArrayElement *last = null;

    MaybeRune r = JSON_popWhitespace(s);
    if(isNone(r)) {
        if(isFail(r, RUNE_EOF)) {
            return fail(JsonValue, mkString("End of file" DEBUG_LOC));
        }
        else {
            return fail(JsonValue, mkString("Invalid character" DEBUG_LOC));
        }
    }
    while(isJust(r) && r.value != ']') {
        JsonValue value = JSON_parseValue(s, alloc);

        if(isNone(value)) {
            return value;
        }

        array.length++;
        JsonArrayElement item = { .value = value };
        JsonArrayElement *pitem = alloc->alloc(*alloc, sizeof(JsonArrayElement));
        *pitem = item;

        if(last == null) {
            array.items = pitem;
            last = pitem;
        }
        else {
            last->next = pitem;
            last = pitem;
        }
        
        r = pstream_peekRune(s);

        if(isJust(r) && r.value == ',') {
            pstream_popRune(s); // pop the comma

            r = JSON_popWhitespace(s);

            if(isJust(r) && r.value == ']') {
                break;
            }
        }
    }

    if(isNone(r)) {
        if(isFail(r, RUNE_EOF)) {
            return fail(JsonValue, mkString("End of file" DEBUG_LOC));
        }
        else {
            return fail(JsonValue, mkString("Invalid character" DEBUG_LOC));
        }
    }

    if(isJust(r) && r.value == ']') {
        pstream_popRune(s);
    }

    JsonArray *parray = alloc->alloc(*alloc, sizeof(JsonArray));
    *parray = array;

    JsonValue result = { .type = JSON_ARRAY, .array = parray };
    return result;
}

JsonValue JSON_parseString(PeekStream *s, Alloc *alloc) {
    pstream_popRune(s); // pop the double-quote

    StringBuilder sb = mkStringBuilder();
    sb.alloc = alloc;

    MaybeRune r;
    while(!(r = pstream_peekRune(s)).error && !JSON_isControl(r.value) && r.value != '\"') {
        pstream_popRune(s);
        if(r.value == '\\') {
            // TODO: escape sequences
            continue;
        }

        sb_appendRune(&sb, r.value);
    }

    if(isJust(r) && r.value == '\"') {
        pstream_popRune(s);
        JsonValue result = { .type = JSON_STRING, .string = sb_build(sb) };
        return result;
    }

    if(isJust(r) && JSON_isControl(r.value)) {
        return fail(JsonValue, mkString("A control character within a sequence" DEBUG_LOC));
    }

    if(isNone(r)) {
        return fail(JsonValue, mkString("A string has not been terminated" DEBUG_LOC));
    }

    return fail(JsonValue, mkString("An unexpected error" DEBUG_LOC));
}

JsonValue JSON_parseValue(PeekStream *s, Alloc *alloc) {
    MaybeRune r = JSON_popWhitespace(s);
    if(isNone(r)) {
        if(isFail(r, RUNE_EOF)) {
            return fail(JsonValue, mkString("End of file" DEBUG_LOC));
        }
        else {
            return fail(JsonValue, mkString("Invalid character" DEBUG_LOC));
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
            return fail(JsonValue, mkString("An unexpected identifier" DEBUG_LOC));
        }
    }
    else if(r.value == 'n') {
        identifier = JSON_parseIdentifier(s, alloc);
        if(str_equal(identifier, mkString("null"))) {
            result = (JsonValue){ .type = JSON_NULL };
        }
        else {
            return fail(JsonValue, mkString("An unexpected identifier" DEBUG_LOC));
        }
    }
    else if(JSON_startNumber(r.value)) {
        result = JSON_parseNumber(s);
    }
    else {
        return fail(JsonValue, mkString("Unexpected character" DEBUG_LOC));
    }

    JSON_popWhitespace(s);

    return result;
}

JsonValue JSON_parse(PeekStream *s, Alloc *alloc) {
    JsonValue value = JSON_parseValue(s, alloc);
    if(isNone(value)) return value;
    MaybeRune r = pstream_peekRune(s);
    if(!isFail(r, RUNE_EOF)) return fail(JsonValue, mkString("Unexpected Character"));
    return value;
}

bool JSON_serializeString(String string, Stream *s) {
    Stream str = mkStreamStr(string);
    if(!stream_writeRune(s, '\"')) return false;
    MaybeRune mr;
    bool result = true;
    // TODO: JSON may allow any byte sequence, gotta check that
    while(isJust(mr = stream_popRune(&str))) {
        rune r = mr.value;
        // TODO: add all escape characters from the spec
        if     (r == '\n') { result = result && stream_writeRune(s, '\\'); result = result && stream_writeRune(s, 'n'); }
        else if(r == '\"') { result = result && stream_writeRune(s, '\\'); result = result && stream_writeRune(s, '\"'); }
        else { result = result && stream_writeRune(s, r); }

        if(!result) return false;
    }

    result = result && stream_writeRune(s, '\"');

    return true;
}

bool JSON_serializeValue(JsonValue value, Stream *s, bool doIndent, usz indent);

bool JSON_serializeArray(JsonValue value, Stream *s, bool doIndent, usz indent) {
    if(value.type != JSON_ARRAY) return false;

    JsonArray array = *value.array;
    JsonArrayElement *elem = array.items;

    bool result = true;
    result = result && stream_writeRune(s, '[');

    if(array.length == 0) { result = result && stream_writeRune(s, ']'); return result; }

    if(doIndent) result = result && stream_writeRune(s, '\n');
    if(!result) return false;

    while(elem) {
        for(int i = 0; doIndent && i < indent + 4; i++) {
            result = result && stream_writeRune(s, ' ');
        }

        result = result && JSON_serializeValue(elem->value, s, doIndent, indent + 4);

        if(elem->next) result = result && stream_writeRune(s, ',');

        if(doIndent) result = result && stream_writeRune(s, '\n');
        if(!result) return false;
        elem = elem->next;
    }

    for(int i = 0; doIndent && i < indent; i++) {
        result = result && stream_writeRune(s, ' ');
    }

    result = result && stream_writeRune(s, ']');
    return result;
}

bool JSON_serializeObject(JsonValue value, Stream *s, bool doIndent, usz indent) {
    if(value.type != JSON_OBJECT) return false;

    JsonObject object = *value.object;
    JsonKeyValue *kv = object.items;

    bool result = true;
    result = result && stream_writeRune(s, '{');

    if(object.length == 0) { result = result && stream_writeRune(s, '}'); return result; }

    if(doIndent) result = result && stream_writeRune(s, '\n');
    if(!result) return false;

    while(kv) {
        for(int i = 0; doIndent && i < indent + 4; i++) {
            result = result && stream_writeRune(s, ' ');
        }

        result = result && JSON_serializeString(kv->key, s);

        if(doIndent) result = result && stream_writeRune(s, ' ');
        result = result && stream_writeRune(s, ':');
        if(doIndent) result = result && stream_writeRune(s, ' ');

        result = result && JSON_serializeValue(kv->value, s, doIndent, indent + 4);

        if(kv->next) result = result && stream_writeRune(s, ',');

        if(doIndent) result = result && stream_writeRune(s, '\n');
        if(!result) return false;
        kv = kv->next;
    }

    for(int i = 0; doIndent && i < indent; i++) {
        result = result && stream_writeRune(s, ' ');
    }

    result = result && stream_writeRune(s, '}');
    return result;
}

bool JSON_serializeNull(JsonValue value, Stream *s) {
    if(value.type != JSON_NULL) return false;
    bool result = true;

    result = result && stream_writeRune(s, 'n');
    result = result && stream_writeRune(s, 'u');
    result = result && stream_writeRune(s, 'l');
    result = result && stream_writeRune(s, 'l');

    return result;
}

bool JSON_serializeBool(JsonValue value, Stream *s) {
    if(value.type != JSON_BOOL) return false;
    bool result = true;

    if(value.boolean) {
        result = result && stream_writeRune(s, 't');
        result = result && stream_writeRune(s, 'r');
        result = result && stream_writeRune(s, 'u');
        result = result && stream_writeRune(s, 'e');
    }
    else {
        result = result && stream_writeRune(s, 'f');
        result = result && stream_writeRune(s, 'a');
        result = result && stream_writeRune(s, 'l');
        result = result && stream_writeRune(s, 's');
        result = result && stream_writeRune(s, 'e');
    }

    return result;
}

bool JSON_serializeValue(JsonValue value, Stream *s, bool doIndent, usz indent) {
    if(isNone(value)) return false;

    if(value.type == JSON_OBJECT) JSON_serializeObject(value, s, doIndent, indent);
    else if(value.type == JSON_STRING) JSON_serializeString(value.string, s);
    else if(value.type == JSON_ARRAY) JSON_serializeArray(value, s, doIndent, indent);
    else if(value.type == JSON_NULL) JSON_serializeNull(value, s);
    else if(value.type == JSON_BOOL) JSON_serializeBool(value, s);
    else { return false; }
}

bool JSON_serialize(JsonValue value, Stream *s, bool doIndent) {
    if(isNone(value)) return false;
    JSON_serializeValue(value, s, doIndent, 0);
}
