#ifndef ZIR_C_LOWER_H
#define ZIR_C_LOWER_H

#include "zir.h"

/* Cross-module symbol table: for each parsed module (by its slash-path),
 * every function's .kry name -> full C name. Built by main after parsing all
 * inputs; used to resolve alias-qualified calls (start.draw_start_page(...)
 * -> ide_start_page_draw_start_page_kry_draw(...)). */
typedef struct ZirCModuleSyms {
    char module_stem[ZIR_PATH_MAX];  /* source path minus .kry */
        char module_slash[ZIR_PATH_MAX];
    struct {
        char kry[ZIR_NAME_MAX];
        char c[ZIR_NAME_MAX * 3];
    } fns[256];
    int fn_count;
} ZirCModuleSyms;

/* Lower a ZirProgram to C source (.c/.h). restab/restab_count resolve
 * cross-module calls; pass NULL/0 to resolve same-module only. */
void zir_c_lower(const ZirProgram *program, const char *root,
               const char *out_dir, const ZirCModuleSyms *restab,
               int restab_count);

/* Build the symbol table entry for one program into out. */
void zir_c_build_syms(const ZirProgram *program, ZirCModuleSyms *out);

/* Full C name for a function (module prefix, colon naming, _kry_draw). */
void zir_c_function_c_name(const ZirModule *m, const ZirFunction *fn,
                         char *dst, size_t dst_size);

/* zir_c_project.c — kryon_project.h/.c (app-host ABI) after all files lower. */
void zir_c_write_project(ZirProgram *const *progs, int prog_count,
                       const char *root, const char *out_dir, int no_main);

#endif
