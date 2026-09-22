#ifndef ZIRAN_BOUNDS_H
#define ZIRAN_BOUNDS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Debug bounds checking for .zi fixed-array and string indexing. Builds
 * that define ZIRAN_BOUNDS_CHECK trap on out-of-range indexes with the
 * offending expression; release builds compile to plain indexing. */
#ifdef ZIRAN_BOUNDS_CHECK
#include <stdio.h>
#include <stdlib.h>
static inline size_t ZiranBoundsCheck(size_t length, size_t index,
                                       const char *what)
{
    if(index >= length) {
        fprintf(stderr, "ziran: index %zu out of bounds for %s (length %zu)\n",
                index, what, length);
        abort();
    }
    return index;
}
#define ZIRAN_INDEX(base, length, index) \
    ((base)[ZiranBoundsCheck((size_t)(length), (size_t)(index), #base)])
#else
#define ZIRAN_INDEX(base, length, index) ((base)[(index)])
#endif

#ifdef __cplusplus
}
#endif

#endif /* ZIRAN_BOUNDS_H */
