#include "zir_text.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

int
is_ident_char(int c)
{
    return isalnum((unsigned char)c) || c == '_';
}

const char *
skip_ws(const char *s)
{
    while(s != NULL && isspace((unsigned char)*s))
        s++;
    return s;
}

const char *
skip_inline_ws(const char *s)
{
    while(s != NULL && (*s == ' ' || *s == '\t'))
        s++;
    return s;
}

char *
trim(char *s)
{
    char *e;

    if(s == NULL)
        return NULL;
    while(isspace((unsigned char)*s))
        s++;
    e = s + strlen(s);
    while(e > s && isspace((unsigned char)e[-1]))
        *--e = '\0';
    return s;
}

char *
trim_in_place(char *s)
{
    char *p;

    if(s == NULL)
        return NULL;
    p = trim(s);
    if(p != s)
        memmove(s, p, strlen(p) + 1);
    return s;
}

void
strip_block_brace(char *s)
{
    size_t n;

    if(s == NULL)
        return;
    n = strlen(s);
    while(n > 0 && isspace((unsigned char)s[n - 1]))
        n--;
    if(n > 0 && s[n - 1] == '{')
        n--;
    while(n > 0 && isspace((unsigned char)s[n - 1]))
        n--;
    s[n] = '\0';
}

void
camel_ident(const char *s, char *dst, size_t dst_size)
{
    size_t n = 0;
    int up = 1;

    if(dst_size == 0)
        return;
    for(const char *p = s; p != NULL && *p != '\0' && n + 1 < dst_size; p++) {
        if(isalnum((unsigned char)*p)) {
            dst[n++] = up ? (char)toupper((unsigned char)*p) : *p;
            up = 0;
        } else {
            up = 1;
        }
    }
    if(n == 0 && n + 1 < dst_size)
        dst[n++] = 'X';
    if(n > 0 && isdigit((unsigned char)dst[0]) && n + 1 < dst_size) {
        memmove(dst + 1, dst, n + 1);
        dst[0] = 'M';
        n++;
    }
    dst[n] = '\0';
}

void
go_field_ident(const char *s, char *dst, size_t dst_size)
{
    static const struct {
        const char *camel;
        const char *go;
    } initialisms[] = {
        {"Id", "ID"},
        {"FocusId", "FocusID"},
        {"MenuId", "MenuID"},
        {"SelectedId", "SelectedID"},
        {"ActivatedId", "ActivatedID"},
        {"CanonicalUrl", "CanonicalURL"},
    };

    if(dst_size == 0)
        return;
    camel_ident(s, dst, dst_size);
    for(size_t i = 0; i < sizeof(initialisms) / sizeof(initialisms[0]); i++) {
        if(strcmp(dst, initialisms[i].camel) == 0) {
            snprintf(dst, dst_size, "%s", initialisms[i].go);
            return;
        }
    }
}

/* Escape s for safe inclusion inside a C string literal in generated code:
 * quotes, backslashes, and non-printable bytes cannot terminate or alter the
 * surrounding literal. Returns the number of chars written; dst always holds
 * a NUL-terminated prefix of the escaped text when dst_size > 0. */
size_t
escape_c_string(const char *s, char *dst, size_t dst_size)
{
    size_t n = 0;

    if(dst_size == 0)
        return 0;
    for(const char *p = s != NULL ? s : ""; *p != '\0'; p++) {
        char buf[8];
        const char *rep = NULL;

        switch(*p) {
        case '"': rep = "\\\""; break;
        case '\\': rep = "\\\\"; break;
        case '\n': rep = "\\n"; break;
        case '\r': rep = "\\r"; break;
        case '\t': rep = "\\t"; break;
        default:
            if((unsigned char)*p < 0x20 || (unsigned char)*p == 0x7f) {
                snprintf(buf, sizeof(buf), "\\x%02x", (unsigned char)*p);
                rep = buf;
            }
            break;
        }
        if(rep == NULL) {
            buf[0] = *p;
            buf[1] = '\0';
            rep = buf;
        }
        for(const char *q = rep; *q != '\0'; q++) {
            if(n + 1 < dst_size)
                dst[n++] = *q;
        }
    }
    dst[n] = '\0';
    return n;
}

int
split_top_level(const char *s, char *parts, int max, size_t part_size)
{
    int depth = 0;
    int n = 0;
    const char *start = s;

    if(s == NULL || parts == NULL || max <= 0 || part_size == 0)
        return 0;
    for(const char *p = s; ; p++) {
        if(*p == '"' || *p == '\'') {
            char q = *p++;

            while(*p != '\0' && *p != q) {
                if(*p == '\\' && p[1] != '\0')
                    p++;
                p++;
            }
        } else if(*p == '\0' || (*p == ',' && depth == 0)) {
            size_t len = (size_t)(p - start);

            if(n < max && len < part_size) {
                char *part = parts + ((size_t)n * part_size);

                memcpy(part, start, len);
                part[len] = '\0';
                trim_in_place(part);
                n++;
            }
            if(*p == '\0')
                break;
            start = p + 1;
        } else if(*p == '(' || *p == '[' || *p == '{') {
            depth++;
        } else if(*p == ')' || *p == ']' || *p == '}') {
            if(depth > 0)
                depth--;
        }
    }
    return n;
}

char *
top_level_assignment(char *s)
{
    int depth = 0;
    for(char *p = s; *p != '\0'; p++) {
        if(*p == '"' || *p == '\'') {
            char quote = *p++;
            while(*p != '\0' && *p != quote) {
                if(*p == '\\' && p[1] != '\0') p++;
                p++;
            }
            if(*p == '\0') break;
        } else if(*p == '(' || *p == '[' || *p == '{') {
            depth++;
        } else if(*p == ')' || *p == ']' || *p == '}') {
            if(depth > 0) depth--;
        } else if(*p == '=' && depth == 0 && p[1] != '=' &&
                  (p == s || (p[-1] != '=' && p[-1] != '!' &&
                              p[-1] != '<' && p[-1] != '>'))) {
            return p;
        }
    }
    return NULL;
}

/* Decode one checked source literal into immutable UTF-8 bytes. The same
 * escapes and Unicode validity rules apply to native and portable strings. */
int
DecodeStringLiteral(const char *source, unsigned char *out, size_t capacity,
              size_t *length)
{
    size_t size = strlen(source);
    size_t used = 0;
    int remaining = 0;
    unsigned int scalar = 0, minimum = 0;
    if(size < 2 || source[0] != '"' || source[size - 1] != '"')
        return 0;
    for(size_t i = 1; i + 1 < size; i++) {
        unsigned int value = (unsigned char)source[i];
        int unicode_escape = 0;
        if(value == '\\') {
            if(++i + 1 >= size)
                return 0;
            value = (unsigned char)source[i];
            const char *escapes = "0abfnrtv\\\"";
            const unsigned char values[] = {0, 7, 8, 12, 10, 13, 9, 11, '\\', '"'};
            const char *found = strchr(escapes, (int)value);
            if(found != NULL) {
                value = values[found - escapes];
            } else if(value == 'x' || value == 'u' || value == 'U') {
                unicode_escape = value != 'x';
                int digits = value == 'x' ? 2 : value == 'u' ? 4 : 8;
                value = 0;
                for(int d = 0; d < digits; d++) {
                    if(++i + 1 >= size)
                        return 0;
                    int digit = (unsigned char)source[i];
                    if(digit >= '0' && digit <= '9') digit -= '0';
                    else if(digit >= 'a' && digit <= 'f') digit -= 'a' - 10;
                    else if(digit >= 'A' && digit <= 'F') digit -= 'A' - 10;
                    else return 0;
                    value = value * 16 + (unsigned int)digit;
                }
                if(value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff))
                    return 0;
            } else {
                return 0;
            }
        } else if(value == '"') {
            return 0;
        }
        unsigned char bytes[4] = {(unsigned char)value, 0, 0, 0};
        int count = 1;
        if(unicode_escape && value >= 128) {
            if(value < 0x800) {
                bytes[0] = 0xc0 | (value >> 6);
                bytes[1] = 0x80 | (value & 63);
                count = 2;
            } else if(value < 0x10000) {
                bytes[0] = 0xe0 | (value >> 12);
                bytes[1] = 0x80 | ((value >> 6) & 63);
                bytes[2] = 0x80 | (value & 63);
                count = 3;
            } else {
                bytes[0] = 0xf0 | (value >> 18);
                bytes[1] = 0x80 | ((value >> 12) & 63);
                bytes[2] = 0x80 | ((value >> 6) & 63);
                bytes[3] = 0x80 | (value & 63);
                count = 4;
            }
        }
        for(int d = 0; d < count; d++) {
            unsigned char byte = bytes[d];
            if(remaining) {
                if((byte & 0xc0) != 0x80)
                    return 0;
                scalar = (scalar << 6) | (byte & 63);
                remaining--;
                if(!remaining && (scalar < minimum || scalar > 0x10ffff ||
                    (scalar >= 0xd800 && scalar <= 0xdfff)))
                    return 0;
            } else if(byte >= 128) {
                if(byte >= 0xc2 && byte <= 0xdf) {
                    remaining = 1;
                    scalar = byte & 31;
                    minimum = 0x80;
                } else if(byte >= 0xe0 && byte <= 0xef) {
                    remaining = 2;
                    scalar = byte & 15;
                    minimum = 0x800;
                } else if(byte >= 0xf0 && byte <= 0xf4) {
                    remaining = 3;
                    scalar = byte & 7;
                    minimum = 0x10000;
                } else {
                    return 0;
                }
            }
            if(used >= capacity)
                return 0;
            out[used++] = byte;
        }
    }
    if(remaining)
        return 0;
    *length = used;
    return 1;
}
