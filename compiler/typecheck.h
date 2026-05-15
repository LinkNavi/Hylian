#ifndef TYPECHECK_H
#define TYPECHECK_H
#include "ast.h"

/* Run the type inference pass over a fully-merged ProgramNode.
   Annotates every ASTNode's resolved_type in place.
   Emits coloured error/warning diagnostics to stderr for type errors,
   undefined variables, unknown functions, missing fields, etc.
   filename is used in diagnostic messages (pass the source file path). */
void typecheck(ProgramNode *program, const char *filename);

#endif