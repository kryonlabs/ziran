#ifndef ZIRAN_BOUNDS_H
#define ZIRAN_BOUNDS_H

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Debug bounds checking for .zi fixed-array and string indexing. Builds
 * that define ZIRAN_BOUNDS_CHECK trap on out-of-range indexes with the
 * offending expression; release builds compile to plain indexing. */
#ifdef ZIRAN_BOUNDS_CHECK
static inline size_t BoundsCheck(size_t length, size_t index,
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
    ((base)[BoundsCheck((size_t)(length), (size_t)(index), #base)])
#else
#define ZIRAN_INDEX(base, length, index) ((base)[(index)])
#endif

/* A zero-capacity array has no valid index in any build configuration. */
static inline size_t EmptyArrayIndex(size_t index, const char *what)
{
    fprintf(stderr, "ziran: index %zu out of bounds for %s (length 0)\n",
            index, what);
    abort();
}
#define ZIRAN_EMPTY_INDEX(base, index) \
    ((base)[EmptyArrayIndex((size_t)(index), #base)])

#ifdef __cplusplus
}
#endif

#endif /* ZIRAN_BOUNDS_H */
