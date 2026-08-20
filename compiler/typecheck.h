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

/* Returns 1 when the named function is a bodyless declaration that came from
   an include file. Generated .hyi declarations for C libraries have this
   shape, and lowering needs to use the platform C ABI for their arguments. */
int tc_func_is_external_decl(const char *name);

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

/* Struct/class fields the LSP knows about from a source it can't merge into
 * the AST it typechecks (vendor .hyi files scanned as text). Without this,
 * member access on a vendor type (e.g. `color.r` on raylib's `Color`) has
 * nowhere to look the field up and gets a false "no field 'r' on type
 * 'Color'" — register_field() only ever sees fields declared in the
 * program being typechecked itself. */
typedef struct {
    const char *class_name;
    const char *field_name;
    const char *type_name;  /* NULL/empty = unknown, resolved permissively */
} TCExternalField;

/* Borrowed, not copied — the array must outlive the typecheck() call.
   Pass NULL/0 to clear. */
void tc_set_external_fields(const TCExternalField *fields, int count);

#endif
