#ifndef CODEGEN_H
#define CODEGEN_H

#include "ast.h"
#include <stdio.h>

void codegen(ProgramNode* root, FILE* out);
void codegen_class(ClassNode* cls, FILE* out);
void codegen_method(MethodNode* method, const char* class_name, FILE* out);
void codegen_field(FieldNode* field, FILE* out);
void codegen_stmt(ASTNode* stmt, FILE* out);
void codegen_expr(ASTNode* expr, FILE* out);

#endif
