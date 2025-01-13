#include <alloc.h>
#include <str.h>
#include <stdio.h>

typedef struct JsonValue JsonValue;

typedef struct JsonKeyValue JsonKeyValue;
struct JsonKeyValue {
    bool error;

    String key;
    JsonValue *value;

    JsonKeyValue *next;
};

typedef struct JsonArrayElement JsonArrayElement;
struct JsonArrayElement {
    bool error;
    JsonValue *value;

    JsonArrayElement *next;
};

typedef struct {
    bool error;
    JsonKeyValue *items;
} JsonObject;

typedef struct {
    bool error;
    JsonArrayElement *items;
} JsonArray;

typedef u8 JsonValueType;
#define JSON_NULL 0
#define JSON_STRING 1
#define JSON_NUMBER 2
#define JSON_OBJECT 3
#define JSON_ARRAY 4
#define JSON_BOOL 5
struct JsonValue {
    bool error;

    JsonValueType type;
    union {
        String string;
        i64 number;
        f64 fnumber;
        JsonObject object;
        JsonArray array;
        bool boolean;
    };
};

bool JSON_isWhitespace(rune r) {
    return r == ' ' || r == '\t' || r == '\n' || r == '\r';
}

// TODO: figure out the extensive list of control characters that JSON disallows in the strings
bool JSON_isControl(rune r) {
    return r == '\n' && r == '\r';
}

JsonValue JSON_parseString(PeekStream *s) {
    pstream_popRune(s); // pop the double-quote

    StringBuilder sb = mkStringBuilder();
    MaybeRune r;
    while(!(r = pstream_peekRune(s)).error && !JSON_isControl(r.value) && r.value != '\"') {
        pstream_popRune(s);
        if(r.value == '\\') {
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
        return none(JsonValue);
    }

    if(r.error) {
        return none(JsonValue);
    }

    return none(JsonValue);
}

JsonValue JSON_parseValue(PeekStream *s) {
    MaybeRune r;
    while(!(r = pstream_peekRune(s)).error && JSON_isWhitespace(r.value)) { 
        pstream_popRune(s);
    }

    if(r.error) { return none(JsonValue); }

    JsonValue result;
    if(r.value == '\"') {
        // JSON string
        JsonValue result = JSON_parseString(s);
    }
    else if(r.value == '{') {
        // JSON object
    }
    else if(r.value == '[') {
        // JSON array
    }
    else if(r.value == 't' || r.value == 'f') {
        // JSON boolean
    }
    else if(r.value == 'n') {
        // JSON null
    }
    else if(false) {
        // JSON number
    }

    while(!(r = pstream_peekRune(s)).error && JSON_isWhitespace(r.value)) { 
        pstream_popRune(s);
    }

    return result;
}
