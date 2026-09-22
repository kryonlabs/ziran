#include "zir_bundle.h"
#include "zir_check.h"
#include "zir_diagnostic.h"
#include "zir_serial.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum { ZIB_VERSION = 1, ZIB_MAX_IR_BYTES = 256 * 1024 * 1024 };

static int
write_u32(FILE *out, uint32_t value)
{
    for(int i = 0; i < 4; i++) {
        if(fputc((int)(value & 255u), out) == EOF)
            return 0;
        value >>= 8;
    }
    return 1;
}

static int
read_u32(FILE *in, uint32_t *value)
{
    uint32_t result = 0;
    for(int i = 0; i < 4; i++) {
        int byte = fgetc(in);
        if(byte == EOF)
            return 0;
        result |= (uint32_t)(unsigned char)byte << (8 * i);
    }
    *value = result;
    return 1;
}

static int
write_name(FILE *out, const char *name)
{
    size_t length = strnlen(name, ZIR_NAME_MAX);
    return length > 0 && length < ZIR_NAME_MAX &&
           write_u32(out, (uint32_t)length) &&
           fwrite(name, 1, length, out) == length;
}

static int
read_name(FILE *in, char *name, size_t capacity)
{
    uint32_t length;
    if(!read_u32(in, &length) || length == 0 || length >= capacity)
        return 0;
    if(fread(name, 1, length, in) != length || memchr(name, 0, length))
        return 0;
    name[length] = 0;
    return 1;
}

static int
copy_bytes(FILE *in, FILE *out, uint32_t count)
{
    unsigned char buffer[8192];
    while(count > 0) {
        size_t amount = count < sizeof(buffer) ? count : sizeof(buffer);
        if(fread(buffer, 1, amount, in) != amount ||
           fwrite(buffer, 1, amount, out) != amount)
            return 0;
        count -= (uint32_t)amount;
    }
    return 1;
}

int
BundleWrite(FILE *out, const ZirProgram *program,
               const char *entry_module, const char *entry_function)
{
    FILE *payload;
    long length;
    int ok;
    if(out == NULL || program == NULL || entry_module == NULL ||
       entry_function == NULL)
        return 0;
    payload = tmpfile();
    if(payload == NULL)
        return 0;
    ok = ProgramWriteZir(program, payload);
    length = ok ? ftell(payload) : -1;
    if(length <= 0 || length > ZIB_MAX_IR_BYTES || fseek(payload, 0, SEEK_SET))
        ok = 0;
    if(ok) {
        ok = fwrite("ZIB\0", 1, 4, out) == 4 &&
             write_u32(out, ZIB_VERSION) &&
             write_name(out, entry_module) &&
             write_name(out, entry_function) &&
             write_u32(out, 0) && /* explicit host capability count */
             write_u32(out, (uint32_t)length) &&
             copy_bytes(payload, out, (uint32_t)length) &&
             fflush(out) == 0;
    }
    fclose(payload);
    return ok;
}

ZirProgram *
BundleRead(FILE *in, const char *path,
              char *entry_module, size_t module_size,
              char *entry_function, size_t function_size)
{
    unsigned char signature[4];
    uint32_t version, capability_count, length;
    FILE *payload = NULL;
    ZirProgram *program = NULL;
    const char *problem = "invalid or truncated bundle";
    if(in == NULL || path == NULL || entry_module == NULL ||
       entry_function == NULL)
        return NULL;
    if(fread(signature, 1, 4, in) != 4 || memcmp(signature, "ZIB\0", 4))
        goto failed;
    if(!read_u32(in, &version))
        goto failed;
    if(version != ZIB_VERSION) {
        problem = "unsupported ZIB version";
        goto failed;
    }
    if(!read_name(in, entry_module, module_size) ||
       !read_name(in, entry_function, function_size) ||
       !read_u32(in, &capability_count))
        goto failed;
    if(capability_count != 0) {
        problem = "bundle requires unsupported host capabilities";
        goto failed;
    }
    if(!read_u32(in, &length) || length == 0 || length > ZIB_MAX_IR_BYTES)
        goto failed;
    payload = tmpfile();
    if(payload == NULL || !copy_bytes(in, payload, length) ||
       fgetc(in) != EOF || ferror(in) || fseek(payload, 0, SEEK_SET))
        goto failed;
    program = ProgramReadZir(payload, path);
    if(program == NULL) {
        problem = "invalid embedded ZIR";
        goto failed;
    }
    if(!LinkImports(&program, 1)) {
        problem = "unresolved bundle import";
        goto failed;
    }
    fclose(payload);
    return program;
failed:
    Diagnostic(Span(path, 1, 1), "zib.invalid", "%s", problem);
    if(payload != NULL)
        fclose(payload);
    ProgramFree(program);
    return NULL;
}
