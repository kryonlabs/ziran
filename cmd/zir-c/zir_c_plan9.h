#ifndef ZIR_C_PLAN9_H
#define ZIR_C_PLAN9_H

/* Post-lowering pass that makes zir_c output safe for the native Plan 9
 * compilers (8c has no __auto_type, no multi-designator compound
 * literals, and no declarations inside for()). Only runs under --plan9;
 * the default output is byte-identical to before.
 *
 * The pass is purely syntactic: types are recovered from cast-literal
 * expressions or from a return-type map built by scanning the project's
 * headers (--include-dir), the same way the kryon surface declares
 * single-line prototypes. */

/* Add a directory scanned (recursively, .h files) for prototypes when
 * resolving __auto_type initializers. Call before zir_c_lower. */
void zir_c_plan9_add_include_dir(const char *dir);

/* Enable --plan9 output post-processing (off by default; the default
 * output is unchanged). */
void zir_c_plan9_set_enabled(int enabled);
int zir_c_plan9_enabled(void);

/* Rewrite one generated C source for 8c. Returns a malloc'd buffer; the
 * caller frees. Returns NULL on out-of-memory (caller keeps original). */
char *zir_c_plan9_rewrite(const char *text);

/* Rewrite a generated file in place when the plan9 pass is enabled.
 * Returns 0 on success. */
int zir_c_plan9_rewrite_file(const char *path);

/* Number of __auto_type declarations left unresolved by the last
 * zir_c_plan9_rewrite call (reported once per run by main). */
int zir_c_plan9_unresolved(void);

#endif
