#ifndef ZIRAN_ZIR_TEXT_H
#define ZIRAN_ZIR_TEXT_H

#include <stddef.h>

int zir_is_ident_char(int c);
const char *zir_skip_ws(const char *s);
const char *zir_skip_inline_ws(const char *s);
char *zir_trim(char *s);
char *zir_trim_in_place(char *s);
void zir_strip_block_brace(char *s);
void zir_camel_ident(const char *s, char *dst, size_t dst_size);
/* One Go field spelling for declarations, initializers, reads, and writes. */
void zir_go_field_ident(const char *s, char *dst, size_t dst_size);
size_t zir_escape_c_string(const char *s, char *dst, size_t dst_size);
int zir_split_top(const char *s, char *parts, int max, size_t part_size);

#endif /* ZIRAN_ZIR_TEXT_H */
