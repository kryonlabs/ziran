#ifndef ZIRAN_ZIR_TEXT_H
#define ZIRAN_ZIR_TEXT_H

#include <stddef.h>

int is_ident_char(int c);
const char *skip_ws(const char *s);
const char *skip_inline_ws(const char *s);
char *trim(char *s);
char *trim_in_place(char *s);
void strip_block_brace(char *s);
void camel_ident(const char *s, char *dst, size_t dst_size);
/* One Go field spelling for declarations, initializers, reads, and writes. */
void go_field_ident(const char *s, char *dst, size_t dst_size);
size_t escape_c_string(const char *s, char *dst, size_t dst_size);
int split_top_level(const char *s, char *parts, int max, size_t part_size);

#endif /* ZIRAN_ZIR_TEXT_H */
