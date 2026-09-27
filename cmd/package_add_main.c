#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "package_add.h"

int
main(int argc, char **argv)
{
    if(argc < 2)
        return 2;
    String launcher = StringView(argv[1], strlen(argv[1]));
    String *arguments = calloc((size_t)(argc - 2), sizeof(*arguments));
    if(argc > 2 && !arguments)
        return 2;
    for(int i = 2; i < argc; i++)
        arguments[i - 2] = StringView(argv[i], strlen(argv[i]));
    Slice slice = {arguments, argc - 2};
    int result = Add(launcher, slice);
    free(arguments);
    return result;
}
