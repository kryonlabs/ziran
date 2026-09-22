#ifndef ZIR_CPP_LOWER_H
#define ZIR_CPP_LOWER_H

#include "zir.h"

/* Cross-module symbol table: for each parsed module (by its slash-path),
 * every function's .kry name -> full C name. Built by main after parsing all
 * inputs; used to resolve alias-qualified calls (start.draw_start_page(...)
 * -> ide_start_page_draw_start_page_kry_draw(...)). */
typedef struct ZirCppModuleSyms {
    char module_stem[ZIR_PATH_MAX];  /* source path minus .kry */
        char module_slash[ZIR_PATH_MAX];
    struct {
        char kry[ZIR_NAME_MAX];
        char c[ZIR_NAME_MAX * 3];
    } fns[256];
    int fn_count;
} ZirCppModuleSyms;

/* Lower a ZirProgram to C++ source (.cpp/.hpp). restab/restab_count resolve
 * cross-module calls; pass NULL/0 to resolve same-module only. */
void zir_cpp_lower(const ZirProgram *program, const char *root,
               const char *out_dir, const ZirCppModuleSyms *restab,
               int restab_count);

/* Build the symbol table entry for one program into out. */
void zir_cpp_build_syms(const ZirProgram *program, ZirCppModuleSyms *out);

/* Full C name for a function (module prefix, colon naming, _kry_draw). */
void zir_cpp_function_c_name(const ZirModule *m, const ZirFunction *fn,
                         char *dst, size_t dst_size);

#endif
