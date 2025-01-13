#include <alloc.h>
#include <str.h>
#include <stdio.h>

typedef struct JsonValue JsonValue;

typedef struct JsonKeyValue JsonKeyValue;
struct JsonKeyValue {
    str key;
    JsonValue *value;

    JsonKeyValue *next;
};

typedef struct JsonArrayElement JsonArrayElement;
struct JsonArrayElement {
    JsonValue *value;

    JsonArrayElement *next;
};

typedef struct {
    JsonKeyValue *items;
} JsonObject;

typedef struct {
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
    JsonValueType type;
    union {
        str string;
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

JsonValue JSON_parseValue(PeekStream *s) {
    MaybeRune r;
    while(!(r = pstream_peekRune(s)).error && JSON_isWhitespace(r.value)) { 
        pstream_pop(s);
    }

    if(r.error) { return 0; }

    if(r.value == '\"') {

    }
}
