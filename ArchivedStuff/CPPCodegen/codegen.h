#ifndef CODEGEN_H
#define CODEGEN_H

#include "ast.h"
#include <stdio.h>

void codegen(ProgramNode *root, FILE *out);
void codegen_class(ClassNode *cls, FILE *out);
void codegen_method(MethodNode *method, FILE *out);
void codegen_func(FuncNode *func, FILE *out);
void codegen_field(FieldNode *field, FILE *out);
void codegen_stmt(ASTNode *stmt, FILE *out, int depth);
void codegen_expr(ASTNode *expr, FILE *out);
void codegen_args(ASTNode **args, int count, FILE *out);
char *codegen_type(Type t);   /* returns heap-allocated C++ type string */

#endif
