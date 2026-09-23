/*
 * zir.h - Ziran intermediate representation.
 *
 * ZIR is the shared compiler representation for .zi source and native
 * backends. This module owns the tree shape, allocation, and source spans.
 */
#ifndef ZIRAN_ZIR_H
#define ZIRAN_ZIR_H

#include <stdio.h>

enum {
    ZIR_PATH_MAX = 1024,
    ZIR_NAME_MAX = 128,
    ZIR_TEXT_MAX = 4096
};

typedef enum ZirImportKind {
    ZIR_IMPORT_HEADER = 1,
    ZIR_IMPORT_MODULE,
    ZIR_IMPORT_EXTERN,
    /* Tag 4 belonged to the removed name-based web intrinsic. */
    ZIR_IMPORT_CAPABILITY = 5,
    ZIR_IMPORT_HOST = 6
} ZirImportKind;

typedef enum ZirExternKind {
    ZIR_EXTERN_NONE = 0,
    ZIR_EXTERN_HOST,
    ZIR_EXTERN_GO,
    ZIR_EXTERN_C
} ZirExternKind;

typedef enum ZirStmtKind {
    ZIR_STMT_UNKNOWN = 0,
    ZIR_STMT_BLOCK_OPEN,
    ZIR_STMT_BLOCK_CLOSE,
    ZIR_STMT_DECL,
    ZIR_STMT_ASSIGN,
    ZIR_STMT_EXPR,
    ZIR_STMT_IF,
    ZIR_STMT_WHILE,
    ZIR_STMT_FOR,
    ZIR_STMT_SWITCH,
    ZIR_STMT_CASE,
    ZIR_STMT_RETURN,
    ZIR_STMT_BREAK,
    ZIR_STMT_CONTINUE,
    ZIR_STMT_GOTO,
    ZIR_STMT_LABEL,
    ZIR_STMT_DEFER,
    ZIR_STMT_UNUSED,
    ZIR_STMT_RAW,
    ZIR_STMT_BLOCK_CALL
} ZirStmtKind;

typedef enum ZirExprKind {
    ZIR_EXPR_UNKNOWN = 0,
    ZIR_EXPR_IDENT,
    ZIR_EXPR_INT,
    ZIR_EXPR_FLOAT,
    ZIR_EXPR_STRING,
    ZIR_EXPR_CALL,
    ZIR_EXPR_BINARY,
    ZIR_EXPR_UNARY,
    ZIR_EXPR_MEMBER,
    ZIR_EXPR_POINTER_MEMBER,
    ZIR_EXPR_INDEX,
    ZIR_EXPR_CAST,
    ZIR_EXPR_COMPOUND,
    ZIR_EXPR_SIZEOF,
    ZIR_EXPR_CHAR,
    ZIR_EXPR_CONDITIONAL,
    ZIR_EXPR_POSTFIX,
    ZIR_EXPR_FIELD_INIT,
    ZIR_EXPR_SLICE
} ZirExprKind;

typedef struct ZirSourceSpan {
    char path[ZIR_PATH_MAX];
    int line;
    int column;
    int end_line;
    int end_column;
} ZirSourceSpan;

typedef struct ZirStateField {
    char name[ZIR_NAME_MAX];
    char type[ZIR_NAME_MAX];
    char init[ZIR_TEXT_MAX];
    char guard[ZIR_TEXT_MAX];   /* enclosing '#if' condition (expanded) */
    ZirSourceSpan span;
} ZirStateField;

typedef struct ZirImport {
    ZirImportKind kind;
    ZirExternKind extern_kind;
    int is_public;
    char name[ZIR_NAME_MAX];
    char target[ZIR_PATH_MAX];
    char extern_symbol[ZIR_NAME_MAX];
    char signature[ZIR_TEXT_MAX];
    char args[ZIR_TEXT_MAX];       /* parsed extern parameters */
    char return_type[ZIR_NAME_MAX];
    int required;
    char guard[ZIR_TEXT_MAX];   /* enclosing '#if' condition (expanded) */
    ZirSourceSpan span;
    const struct ZirModule *resolved_module; /* borrowed from the checked program set */
} ZirImport;

typedef struct ZirStmt {
    ZirStmtKind kind;
    char text[ZIR_TEXT_MAX];
    char callee[ZIR_NAME_MAX];
    char args[ZIR_TEXT_MAX];
    int declared_block_call; /* typed block invocation, resolved after imports */
    int expr_root;      /* index into enclosing function exprs, or -1 */
    int lhs_root;       /* structured assignment destination, or -1 */
    char name[ZIR_NAME_MAX]; /* declaration binding */
    char type[ZIR_NAME_MAX]; /* declared or inferred type */
    char assignment_op[4];
    ZirSourceSpan span;
} ZirStmt;

typedef struct ZirExpr {
    ZirExprKind kind;
    int is_function_value; /* declaration bound to an expected slot signature */
    char slot_type[ZIR_NAME_MAX]; /* lexical callable signature, empty for ordinary calls */
    char text[ZIR_TEXT_MAX];
    char name[ZIR_NAME_MAX];
    char op[8];
    int left;
    int right;
    int first_child;
    int next_sibling;
    int third;         /* false conditional arm or slice upper bound, or -1 */
    char type[ZIR_NAME_MAX]; /* resolved type; empty means unresolved */
    ZirSourceSpan span;
} ZirExpr;

typedef struct ZirCapture {
    char name[ZIR_NAME_MAX];
    char type[ZIR_NAME_MAX];
} ZirCapture;

typedef struct ZirFunction {
    char name[ZIR_NAME_MAX];
    char args[ZIR_TEXT_MAX];
    char return_type[ZIR_NAME_MAX];
    int exported;
    int is_extern;
    ZirExternKind extern_kind;
    int is_closure; /* inline slot body, emitted at its lexical binding */
    ZirCapture *captures;
    int capture_count;
    int is_public;  /* function emitted in generated interfaces */
    int checked;    /* shared checker resolved the function without errors */
    int uses_host; /* direct or transitive host services or retained-state access */
    char extern_target[ZIR_NAME_MAX];   /* '#extern "pkg.Fn"' quoted symbol */
    char extern_symbol[ZIR_NAME_MAX];   /* stripped C symbol for c.* externs */
    char guard[ZIR_TEXT_MAX];   /* enclosing '#if' condition (expanded) */
    ZirSourceSpan span;
    ZirStmt *stmts;
    int stmt_count;
    int stmt_cap;
    ZirExpr *exprs;
    int expr_count;
    int expr_cap;
} ZirFunction;

typedef struct ZirGlobal {
    char name[ZIR_NAME_MAX];
    char type[ZIR_TEXT_MAX];
    char init[ZIR_TEXT_MAX];
    int is_static;   /* 'static name: T = init' — internal linkage */
    char guard[ZIR_TEXT_MAX];   /* enclosing '#if' condition (expanded) */
    ZirSourceSpan span;
} ZirGlobal;

/* A Kry `Name :: value` constant emitted into generated target interfaces. */
typedef struct ZirDefine {
    char name[ZIR_NAME_MAX];
    char value[ZIR_TEXT_MAX];
    char guard[ZIR_TEXT_MAX];   /* enclosing '#if' condition (expanded) */
    ZirSourceSpan span;
} ZirDefine;

typedef struct ZirAssert {
    char condition[ZIR_TEXT_MAX]; /* expanded C preprocessor condition */
    char message[ZIR_TEXT_MAX];   /* C #error payload */
    int known;                    /* condition resolved by Kry frontend */
    int value;                    /* boolean value when known */
    char guard[ZIR_TEXT_MAX];     /* enclosing '#if' condition (expanded) */
    ZirSourceSpan span;
} ZirAssert;

/* A `Name :: struct { fields }` type declaration. body holds the raw field
 * lines (one per line, no braces). */
typedef struct ZirType {
    char name[ZIR_NAME_MAX];
    char body[ZIR_TEXT_MAX * 2];
    int is_slot;   /* named, synchronous child-content signature; body holds parameters */
    int is_enum;   /* 'Name :: enum' — emit typedef enum, not struct */
    int is_extern; /* host-owned C record; native Go emits the declared shape */
    char guard[ZIR_TEXT_MAX];   /* enclosing '#if' condition (expanded) */
    ZirSourceSpan span;
} ZirType;

typedef struct ZirTypeField {
    char name[ZIR_NAME_MAX];
    char type[ZIR_NAME_MAX];
} ZirTypeField;

/* Start offset at zero. Returns 1 for a field, 0 at end, -1 for malformed
 * record syntax. Names/types are trimmed without truncating source tokens. */
int TypeNextField(const ZirType *record, size_t *offset, ZirTypeField *field);

/* Parse a fixed-capacity array type text "[N]element". Returns 1 with the
 * element type copied out and the capacity stored, 0 for any other type. */
int SliceElementType(const char *type, char *element, size_t element_size);
int ArrayElementType(const char *type, char *element, size_t element_size,
                        int *capacity);

typedef struct ZirModule {
    char name[ZIR_NAME_MAX];
    char source_path[ZIR_PATH_MAX];
    ZirSourceSpan span;
    ZirGlobal *globals;
    int global_count;
    int global_cap;
    ZirDefine *defines;
    int define_count;
    int define_cap;
    ZirAssert *asserts;
    int assert_count;
    int assert_cap;
    ZirType *types;
    int type_count;
    int type_cap;
    ZirStateField *state_fields;
    int state_count;
    int state_cap;
    ZirImport *imports;
    int import_count;
    int import_cap;
    ZirFunction *functions;
    int function_count;
    int function_cap;
} ZirModule;

typedef struct ZirProgram {
    ZirModule *modules;
    int module_count;
    int module_cap;
} ZirProgram;

/* Local records and direct unqualified Kry imports; owner supplies field scope. */
/* Local declarations shadow imports. Returns 1 found, 0 absent, -1 ambiguous. */
int ResolveFunction(const ZirModule *module, const char *name,
                       const ZirModule **owner, const ZirFunction **function);
/* Runtime contracts are parsed from embedded declaration sources. */
const ZirType *FindType(const ZirModule *module, const char *name,
                          const ZirModule **owner);
int ResolveEnumMember(const ZirModule *module, const char *name,
                         const ZirModule **owner, const ZirType **type);

ZirProgram *ProgramNew(void);
void ProgramFree(ZirProgram *program);
void copy_text(char *dst, size_t dst_size, const char *src);
ZirSourceSpan Span(const char *path, int line, int column);
ZirSourceSpan SpanEnd(const char *path, int line, int column,
                         int end_line, int end_column);
ZirModule *ProgramAddModule(ZirProgram *program, const char *name,
                               const char *source_path, ZirSourceSpan span);
ZirStateField *ModuleAddStateField(ZirModule *module, const char *name,
                                      const char *type, const char *init,
                                      ZirSourceSpan span);
ZirImport *ModuleAddImport(ZirModule *module, ZirImportKind kind,
                              const char *name, const char *target,
                              const char *signature, int required,
                              ZirSourceSpan span);
ZirFunction *ModuleAddFunction(ZirModule *module, const char *name,
                                  const char *args, const char *return_type,
                                  int exported, ZirSourceSpan span);
void ModuleAddGlobal(ZirModule *module, const char *name, const char *type,
                        const char *init, ZirSourceSpan span);
void ModuleAddStatic(ZirModule *module, const char *name, const char *type,
                        const char *init, ZirSourceSpan span);
ZirDefine *ModuleAddDefine(ZirModule *module, const char *name,
                              const char *value, ZirSourceSpan span);
ZirAssert *ModuleAddAssert(ZirModule *module, const char *condition,
                              const char *message, ZirSourceSpan span);
ZirType *ModuleAddType(ZirModule *module, const char *name,
                          ZirSourceSpan span);
ZirStmt *FunctionAddStmt(ZirFunction *fn, ZirStmtKind kind,
                            const char *text, const char *callee,
                            ZirSourceSpan span);
ZirStmt *FunctionAddBlockCall(ZirFunction *fn, const char *callee,
                              const char *args, const char *text,
                              ZirSourceSpan span);
const char *ImportKindName(ZirImportKind kind);
const char *StmtKindName(ZirStmtKind kind);
const char *ExprKindName(ZirExprKind kind);
ZirExpr *FunctionAddExpr(ZirFunction *fn, ZirExprKind kind,
                            const char *text, ZirSourceSpan span);
void ProgramDump(const ZirProgram *program, FILE *out);

#endif /* ZIRAN_ZIR_H */
