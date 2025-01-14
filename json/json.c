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
struct JsonValue {
    bool error;

    String errmsg;

    JsonValueType type;
    union {
        String string;
        i64 number;
        f64 fnumber;
        JsonObject *object;
        JsonArray *array;
        bool boolean;
    };
};

typedef struct JsonKeyValue JsonKeyValue;
struct JsonKeyValue {
    String key;
    JsonValue *value;

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

JsonValue JSON_parseValue(PeekStream *s, Alloc *alloc);
JsonValue JSON_parseString(PeekStream *s, Alloc *alloc);

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

MaybeRune JSON_popWhitespace(PeekStream *s) {
    MaybeRune r;
    while(!(r = pstream_peekRune(s)).error && JSON_isWhitespace(r.value)) { 
        pstream_popRune(s);
    }
    return r;
}

JsonValue JSON_parseObject(PeekStream *s, Alloc *alloc) {
    pstream_popRune(s); // pop the opening curly brace

    MaybeRune r = JSON_popWhitespace(s);

    while(isJust(r) && r.value != '}') {
        JsonValue string = JSON_parseString(s, alloc);
        if(isNone(string)) return string;

        r = JSON_popWhitespace(s);

        if(isNone(r) || r != ':') {
            return fail(JsonValue, "Expected a colon :" DEBUG_LOC);
        }

        pstream_popRune(s); // pop the colon

        JsonValue value = JSON_parseValue(s, alloc);

        if(isJust(r) && r.value == ',') {
            pstream_popRune(s); // pop the comma

            r = JSON_popWhitespace(s);

            if(isJust(r) && r.value == '}') {
                break;
            }
        }
    }
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

    if(r.value == '\"') {
        pstream_popRune(s);
        JsonValue result = { .type = JSON_STRING, .string = sb_build(sb) };
        return result;
    }

    if(JSON_isControl(r.value)) {
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
        // JSON string
        result = JSON_parseString(s, alloc);
    }
    else if(r.value == '{') {
        // JSON object
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
            result = fail(JsonValue, mkString("An unexpected identifier" DEBUG_LOC));
        }
    }
    else if(r.value == 'n') {
        identifier = JSON_parseIdentifier(s, alloc);
        if(str_equal(identifier, mkString("null"))) {
            result = (JsonValue){ .type = JSON_NULL };
        }
        else {
            result = fail(JsonValue, mkString("An unexpected identifier" DEBUG_LOC));
        }
    }
    else if(false) {
        // JSON number
    }
    else {
        result = fail(JsonValue, mkString("Unexpected character" DEBUG_LOC));
    }

    JSON_popWhitespace(s);

    return result;
}
