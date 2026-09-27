#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "zir_slice.h"
#include "zir_string.h"
#include "package_lock.h"
#include "package_map.h"

uint64_t ziran_package_lock_size(void) { return sizeof(PkgLock); }
uint64_t ziran_package_graph_size(void) { return sizeof(PkgModuleGraph); }

static char *copy_string(String value)
{
    if(memchr(value.data, 0, value.length)) return NULL;
    char *copy = malloc(value.length + 1);
    if(!copy) return NULL;
    memcpy(copy, value.data, value.length);
    copy[value.length] = 0;
    return copy;
}

/* OS boundary for the Ziran package resolver. No package decisions live here. */
int32_t ziran_package_run(Slice arguments, String directory,
                          Slice names, Slice values, Slice output)
{
    if(arguments.length < 1 || arguments.length > 256 ||
       names.length != values.length || names.length > 32 ||
       output.length < 0) return -1;
    String *input = arguments.data;
    char **argv = calloc((size_t)arguments.length + 1, sizeof(*argv));
    if(!argv) return -1;
    for(int64_t i = 0; i < arguments.length; i++) {
        argv[i] = copy_string(input[i]);
        if(!argv[i]) goto failure;
    }
    int fds[2] = {-1, -1};
    if(output.length > 0 && pipe(fds)) goto failure;
    fflush(NULL);
    pid_t child = fork();
    if(child < 0) goto failure;
    if(child == 0) {
        if(output.length > 0) {
            close(fds[0]);
            if(dup2(fds[1], STDOUT_FILENO) < 0) _exit(127);
            if(dup2(fds[1], STDERR_FILENO) < 0) _exit(127);
            close(fds[1]);
        }
        if(directory.length > 0) {
            char *cwd = copy_string(directory);
            if(!cwd || chdir(cwd)) _exit(127);
        }
        String *keys = names.data, *entries = values.data;
        for(int64_t i = 0; i < names.length; i++) {
            char *key = copy_string(keys[i]);
            char *value = copy_string(entries[i]);
            if(!key || !value || setenv(key, value, 1)) _exit(127);
            free(key);
            free(value);
        }
        execvp(argv[0], argv);
        perror(argv[0]);
        _exit(127);
    }
    int truncated = 0;
    if(output.length > 0) {
        close(fds[1]);
        char *bytes = output.data;
        size_t used = 0, capacity = (size_t)output.length;
        for(;;) {
            char chunk[8192];
            ssize_t count = read(fds[0], chunk, sizeof(chunk));
            if(count < 0 && errno == EINTR) continue;
            if(count < 0) { truncated = 1; break; }
            if(count == 0) break;
            size_t copy = (size_t)count;
            if(copy > capacity - used - 1) {
                copy = capacity - used - 1;
                truncated = 1;
            }
            memcpy(bytes + used, chunk, copy);
            used += copy;
        }
        bytes[used] = 0;
        close(fds[0]);
    }
    int status = 0;
    while(waitpid(child, &status, 0) < 0) {
        if(errno != EINTR) { status = -1; break; }
    }
    for(int64_t i = 0; i < arguments.length; i++) free(argv[i]);
    free(argv);
    if(truncated || status == -1) return -1;
    if(WIFEXITED(status)) return WEXITSTATUS(status);
    if(WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return -1;
failure:
    if(fds[0] >= 0) close(fds[0]);
    if(fds[1] >= 0) close(fds[1]);
    for(int64_t i = 0; i < arguments.length; i++) free(argv[i]);
    free(argv);
    return -1;
}
