#ifndef LOWER_H
#define LOWER_H

#include "ast.h"
#include "ir.h"

/* Lower a fully type-checked AST program into an IRModule.
   The returned module must eventually be passed to ir_module_free(). */
IRModule *lower_program(ProgramNode *prog);

#endif /* LOWER_H */
