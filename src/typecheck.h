#ifndef TYPECHECK_H
#define TYPECHECK_H
#include "ast.h"

/* Run the type inference pass over a fully-merged ProgramNode.
   Annotates every ASTNode's resolved_type in place.
   Prints warnings to stderr for type errors. */
void typecheck(ProgramNode *program);

#endif