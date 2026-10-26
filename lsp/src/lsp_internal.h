#ifndef LSP_INTERNAL_H
#define LSP_INTERNAL_H

#include <time.h>
#include "lsp_analysis.h"
#include "loader.h"

/* ── modules: one parsed file the project knows about ────────────────────── */

typedef struct {
    char         path[1024];
    int          is_hyi;         /* interface file (vendor / std .hyi)          */
    long         stamp;          /* mtime, or the overlay version             */
    char        *text;           /* source text (NULL for .hyi)                */
    ProgramNode *prog;
    char         std_root[1024]; /* for a std module: the stdlib root its own
                                    includes resolve against ("" otherwise)    */
} Module;

#define MAX_OVERLAYS 128
typedef struct {
    char  path[1024];
    char *text;
    long  version;
} Overlay;

struct LspProject {
    char     workspace[1024];
    Module **mods;
    int      mod_count, mod_cap;
    Overlay  overlays[MAX_OVERLAYS];
    int      overlay_count;
    long     next_version;
};

/* every module a file can see through its (transitive) includes */
#define MAX_DEPS 256
typedef struct {
    Module *mods[MAX_DEPS];
    int     count;
} DepSet;

/* ── analysed file ───────────────────────────────────────────────────────── */

struct LspState {
    LspProject *proj;
    char        path[1024];
    char       *text;
    ZoraInfo    zora;
    ProgramNode *prog;        /* the file's own AST (NULL if the parse died)   */
    DepSet      deps;
    /* includes that could not be resolved, with why (also reported as diagnostics) */
    struct { char inc[128]; char reason[400]; } unresolved[16];
    int         unresolved_count;
    LspDiag     diags[LSP_DIAG_MAX];
    int         diag_count;
};

/* lsp_modules.c */
Module *module_get(LspProject *proj, const char *path, int is_hyi);
int     module_exists(const char *path);
void    deps_collect(LspState *st);
ProgramNode *build_check_program(LspState *st);
/* every std module name (dotted, without "std.") the installed stdlib offers */
int     std_module_names(char names[][96], int max);
/* the stdlib root that `std.x` resolves against for this project */
void    std_root_for(const ZoraInfo *z, char *out, size_t n);
/* vendor module names available to the project (dirs with a .hyi) */
/* pending[i] = 1: declared as a Zora dep but its bindings are not generated yet */
int     vendor_module_names(const ZoraInfo *z, char names[][96], char *pending, int max);
/* project modules under src_dir as dotted names */
int     project_module_names(const ZoraInfo *z, const char *current_path, char names[][96], int max);

/* lsp_query.c - text and symbol helpers shared by the features */
typedef struct { const char *text; int len; } TextView;

int   lq_offset(const char *text, int line, int col);
void  lq_position(const char *text, int offset, int *line, int *col);
int   lq_line_bounds(const char *text, int line, int *start, int *end);
/* 0 = code, 1 = comment, 2 = string literal (not inside {{ }}) */
int   lq_lex_state(const char *text, int offset);
int   lq_is_ident(int c);

typedef struct {
    const char *name;
    Type        type;
    int         line, col;      /* declaration position */
    int         is_param;
} ScopeVar;

#define MAX_SCOPE 256
typedef struct {
    ScopeVar vars[MAX_SCOPE];
    int      count;
    FuncNode   *func;        /* enclosing free function, or NULL */
    MethodNode *method;      /* enclosing method, or NULL */
    ClassNode  *cls;         /* enclosing class, or NULL */
    ModuleNode *mod;         /* enclosing module, or NULL */
} Scope;

void  lq_scope_at(LspState *st, int line, int col, Scope *out);

/* iterate every top-level declaration the file can see: fn(decl, module or NULL for own) */
typedef void (*DeclFn)(ASTNode *decl, Module *from, void *user);
void  lq_each_decl(LspState *st, DeclFn fn, void *user);

FuncNode  *lq_find_func(LspState *st, const char *name, Module **from);
ClassNode *lq_find_class(LspState *st, const char *name, Module **from);
EnumNode  *lq_find_enum(LspState *st, const char *name, Module **from);
ModuleNode *lq_find_module(LspState *st, const char *name, Module **from);
StaticVarNode *lq_find_static(LspState *st, const char *name, Module **from);
MethodNode *lq_find_method(ClassNode *cls, const char *name);
FieldNode  *lq_find_field(ClassNode *cls, const char *name);

typedef enum { RES_NONE, RES_TYPE, RES_MODULE, RES_ENUM } ResKind;
typedef struct {
    ResKind kind;
    Type    type;          /* RES_TYPE */
    char    name[128];     /* RES_MODULE / RES_ENUM name, or the type's name */
} Resolved;

/* Resolve the type of the expression text[start, end) at the cursor. */
Resolved lq_resolve_chain(LspState *st, const Scope *sc, const char *text, int start, int end);
/* Start of the postfix expression that ends just before `dot` (the '.') */
int   lq_chain_start(const char *text, int dot);

void  lq_type_str(Type t, char *buf, size_t n);
void  lq_func_sig(const char *name, Type ret, ASTNode **params, int pc, char *buf, size_t n);
/* `//` comment lines directly above line `line` (0-based) of `text` */
void  lq_doc_above(const char *text, int line, char *out, size_t n);

#endif
