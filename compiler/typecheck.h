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

#endif