#ifndef TYPECHECK_H
#define TYPECHECK_H
#include "ast.h"

/* Run the type inference pass over a fully-merged ProgramNode.
   Annotates every ASTNode's resolved_type in place.
   Emits coloured error/warning diagnostics to stderr for type errors,
   undefined variables, unknown functions, missing fields, etc.
   filename is used in diagnostic messages (pass the source file path). */
void typecheck(ProgramNode *program, const char *filename);

/* Returns 1 if the named function is registered and its return type is bool,
   0 otherwise.  Used by lower.c to stamp IR_CALL.extra_int so that codegen
   can emit `movzx rax, al` after calls that return a C _Bool / bool. */
int tc_func_return_is_bool(const char *name);

/* ── externally-supplied symbols ──────────────────────────────────────────────
 *
 * The CLI typechecks a fully-merged program: compile_file() has already pulled
 * every include in, so every symbol is present in the AST. The LSP can't do
 * that — it analyses one open buffer at a time and must not re-parse the whole
 * stdlib on every keystroke. It knows about stdlib symbols from its own index,
 * and registers them here so the shared typechecker can resolve them instead of
 * reporting "undefined variable 'STDERR_FD'" or "no method 'arg' on type 'Cmd'"
 * for code that compiles perfectly well.
 *
 * Methods are registered as ordinary functions named "Class__method", which is
 * exactly how the typechecker looks them up internally.
 */
typedef enum {
    TC_EXT_FUNC,    /* free function, or Class__method */
    TC_EXT_VAR,     /* global constant or variable */
    TC_EXT_MODULE,  /* module name usable as a namespace prefix */
} TCExternalKind;

typedef struct {
    const char    *name;
    const char    *type_name;  /* return type / variable type; NULL = unknown */
    TCExternalKind kind;
    int            param_count; /* TC_EXT_FUNC only; -1 = unknown/any */
} TCExternal;

/* Borrowed, not copied — the array must outlive the typecheck() call.
   Pass NULL/0 to clear. */
void tc_set_externals(const TCExternal *ext, int count);

#endif