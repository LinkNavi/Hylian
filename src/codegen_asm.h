#ifndef CODEGEN_ASM_H
#define CODEGEN_ASM_H
#include "ast.h"
#include <stdio.h>
void codegen_asm(ProgramNode *root, FILE *out, const char *src_filename, const char *target);
#endif