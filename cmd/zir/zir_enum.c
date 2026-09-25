#include "zir.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef struct EnumEntry {
    char name[ZIR_NAME_MAX];
    int64_t value;
} EnumEntry;

static int
fits_backing(const ZirType *type, int64_t value)
{
    const char *backing = type->enum_backing;
    int bits = atoi(backing + 1);
    if((backing[0] != 's' && backing[0] != 'u') ||
       (bits != 8 && bits != 16 && bits != 32 && bits != 64))
        return 0;
    if(backing[0] == 'u')
        return value >= 0 &&
               (bits == 64 || (uint64_t)value < (UINT64_C(1) << bits));
    if(bits == 64) return 1;
    return value >= -(INT64_C(1) << (bits - 1)) &&
           value < (INT64_C(1) << (bits - 1));
}

static void
skip_space(const char **cursor, const char *end)
{
    while(*cursor < end && isspace((unsigned char)**cursor))
        (*cursor)++;
}

static int
term(const char **cursor, const char *end,
     const EnumEntry *entries, int count, int64_t *value)
{
    const char *start;
    int sign = 1;
    skip_space(cursor, end);
    if(*cursor < end && (**cursor == '+' || **cursor == '-')) {
        if(**cursor == '-') sign = -1;
        (*cursor)++;
    }
    skip_space(cursor, end);
    start = *cursor;
    if(start >= end) return 0;
    if(isdigit((unsigned char)*start)) {
        char *after;
        unsigned long long magnitude;
        errno = 0;
        magnitude = strtoull(start, &after, 0);
        if(errno || after == start || after > end ||
           magnitude > (unsigned long long)INT64_MAX + (sign < 0))
            return 0;
        if(sign < 0)
            *value = magnitude == (unsigned long long)INT64_MAX + 1 ?
                INT64_MIN : -(int64_t)magnitude;
        else
            *value = (int64_t)magnitude;
        *cursor = after;
    } else if(isalpha((unsigned char)*start) || *start == '_') {
        while(*cursor < end &&
              (isalnum((unsigned char)**cursor) || **cursor == '_'))
            (*cursor)++;
        int found = 0;
        for(int i = 0; i < count; i++)
            if(strlen(entries[i].name) == (size_t)(*cursor - start) &&
               strncmp(entries[i].name, start,
                       (size_t)(*cursor - start)) == 0) {
                *value = entries[i].value;
                found = 1;
                break;
            }
        if(!found) return 0;
    } else {
        return 0;
    }
    if(sign < 0 && !isdigit((unsigned char)*start)) {
        if(*value == INT64_MIN) return 0;
        *value = -*value;
    }
    return 1;
}

static int
mul_expression(const char **cursor, const char *end,
               const EnumEntry *entries, int count, int64_t *value)
{
    if(!term(cursor, end, entries, count, value)) return 0;
    for(;;) {
        char op;
        int64_t rhs;
        skip_space(cursor, end);
        if(*cursor >= end ||
           (**cursor != '*' && **cursor != '/' && **cursor != '%'))
            return 1;
        op = *(*cursor)++;
        if(!term(cursor, end, entries, count, &rhs))
            return 0;
        if(op == '*') {
            if((*value > 0 && rhs > 0 && *value > INT64_MAX / rhs) ||
               (*value > 0 && rhs < 0 && rhs < INT64_MIN / *value) ||
               (*value < 0 && rhs > 0 && *value < INT64_MIN / rhs) ||
               (*value < 0 && rhs < 0 && *value != 0 &&
                rhs < INT64_MAX / *value))
                return 0;
            *value *= rhs;
        } else {
            if(rhs == 0 || *value == INT64_MIN || rhs == -1)
                return 0;
            *value = op == '/' ? *value / rhs : *value % rhs;
        }
    }
}

static int
add_expression(const char **cursor, const char *end,
               const EnumEntry *entries, int count, int64_t *value)
{
    if(!mul_expression(cursor, end, entries, count, value)) return 0;
    for(;;) {
        int64_t rhs;
        skip_space(cursor, end);
        if(*cursor == end || (**cursor != '+' && **cursor != '-')) return 1;
        char op = *(*cursor)++;
        if(!term(cursor, end, entries, count, &rhs))
            return 0;
        if((op == '+' && ((rhs > 0 && *value > INT64_MAX - rhs) ||
                          (rhs < 0 && *value < INT64_MIN - rhs))) ||
           (op == '-' && ((rhs < 0 && *value > INT64_MAX + rhs) ||
                          (rhs > 0 && *value < INT64_MIN + rhs))))
            return 0;
        if(op == '+') *value += rhs;
        else *value -= rhs;
    }
}

static int
shift_expression(const char **cursor, const char *end,
                 const EnumEntry *entries, int count, int64_t *value)
{
    if(!add_expression(cursor, end, entries, count, value)) return 0;
    for(;;) {
        int64_t amount;
        skip_space(cursor, end);
        if(*cursor + 1 >= end || (*cursor)[0] != '<' ||
           (*cursor)[1] != '<') return 1;
        *cursor += 2;
        if(!add_expression(cursor, end, entries, count, &amount) ||
           amount < 0 || amount >= 63 || *value < 0 ||
           *value > INT64_MAX >> amount)
            return 0;
        *value <<= amount;
    }
}

static int
expression(const char *cursor, const char *end,
           const EnumEntry *entries, int count, int64_t *value)
{
    if(!shift_expression(&cursor, end, entries, count, value)) return 0;
    skip_space(&cursor, end);
    return cursor == end;
}

int
EnumMemberValue(const ZirType *type, const char *wanted, int64_t *value)
{
    if(!type->is_enum) return 0;
    size_t capacity = strlen(type->body) + 1;
    EnumEntry *entries = calloc(capacity, sizeof(*entries));
    if(entries == NULL) return 0;
    int count = 0, found = 0, next_valid = 1;
    int64_t next = 0;
    const char *cursor = type->body;
    while(*cursor) {
        while(*cursor == ',' || isspace((unsigned char)*cursor)) cursor++;
        if(*cursor == 0) break;
        const char *end = cursor;
        while(*end && *end != ',' && *end != '\n') end++;
        const char *start = cursor;
        if(!isalpha((unsigned char)*cursor) && *cursor != '_') goto invalid;
        while(cursor < end &&
              (isalnum((unsigned char)*cursor) || *cursor == '_')) cursor++;
        size_t length = (size_t)(cursor - start);
        if(length == 0 || length >= ZIR_NAME_MAX ||
           (size_t)count >= capacity) goto invalid;
        for(int i = 0; i < count; i++)
            if(strlen(entries[i].name) == length &&
               strncmp(entries[i].name, start, length) == 0)
                goto invalid;
        skip_space(&cursor, end);
        int64_t number = next;
        if(cursor < end) {
            if(*cursor++ != '=' ||
               !expression(cursor, end, entries, count, &number))
                goto invalid;
        } else if(!next_valid) {
            goto invalid;
        }
        if(!fits_backing(type, number)) goto invalid;
        memcpy(entries[count].name, start, length);
        entries[count].name[length] = 0;
        entries[count].value = number;
        if(wanted != NULL && strcmp(wanted, entries[count].name) == 0) {
            if(value != NULL) *value = number;
            found = 1;
        }
        count++;
        next_valid = number != INT64_MAX;
        next = next_valid ? number + 1 : number;
        cursor = *end ? end + 1 : end;
    }
    free(entries);
    return wanted == NULL ? count > 0 : found;
invalid:
    free(entries);
    return 0;
}
