#ifndef CODEGEN_ASM_H
#define CODEGEN_ASM_H
#include "ir.h"
#include <stdio.h>
void codegen_ir(IRModule *mod, FILE *out, const char *src_filename, const char *target);
#endif
