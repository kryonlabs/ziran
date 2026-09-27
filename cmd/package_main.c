#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "package.h"

int main(int argc, char **argv)
{
    String *arguments = calloc((size_t)(argc > 1 ? argc - 1 : 1),
                               sizeof(*arguments));
    if(!arguments) return 2;
    for(int i = 1; i < argc; i++)
        arguments[i - 1] = StringView(argv[i], strlen(argv[i]));
    Slice slice = {arguments, argc - 1};
    int code = PackageMain(StringView(argv[0], strlen(argv[0])), slice);
    free(arguments);
    return code;
}
