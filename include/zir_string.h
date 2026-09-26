#ifndef ZIR_STRING_VALUE_DEFINED
#define ZIR_STRING_VALUE_DEFINED

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Immutable borrowed UTF-8 bytes; length preserves embedded nulls. */
typedef struct String { const char *data; size_t length; } String;
typedef struct Source_Code_Location {
    String fully_pathed_filename;
    int64_t line_number;
} Source_Code_Location;

static inline String StringView(const char *data, size_t length) {
    String value = {data, length};
    return value;
}

/* Keep long literals in generated expressions once, including embedded NUL. */
#define StringLiteral(text) StringView((text), sizeof(text) - 1)

static inline String StringRange(String source, int64_t low, int64_t high) {
    if (low < 0 || high < low || (uint64_t)high > source.length ||
        (source.data == NULL && source.length != 0)) {
        fprintf(stderr, "ziran: string range out of bounds\n");
        abort();
    }
    return StringView(source.data == NULL ? "" : source.data + low,
                      (size_t)(high - low));
}

static inline bool StringEqual(String a, String b) {
    return a.length == b.length &&
        (a.length == 0 || memcmp(a.data, b.data, a.length) == 0);
}

#endif
