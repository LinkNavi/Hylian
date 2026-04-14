#include "ast.h"
#include <stdlib.h>
#include <string.h>

ProgramNode* make_program() {
    ProgramNode* node = malloc(sizeof(ProgramNode));
    node->base.type = NODE_PROGRAM;
    node->declarations = NULL;
    node->decl_count = 0;
    return node;
}

ClassNode* make_class(char* name, int is_public) {
    ClassNode* node = malloc(sizeof(ClassNode));
    node->base.type = NODE_CLASS;
    node->name = strdup(name);
    node->is_public = is_public;
    node->fields = NULL;
    node->field_count = 0;
    node->methods = NULL;
    node->method_count = 0;
    return node;
}
CppIncludeNode* make_cpp_include(char* header) {
    CppIncludeNode* node = malloc(sizeof(CppIncludeNode));
    node->base.type = NODE_CPP_INCLUDE;
    node->header = strdup(header);
    return node;
}
MethodNode* make_method(Type return_type, char* name) {
    MethodNode* node = malloc(sizeof(MethodNode));
    node->base.type = NODE_METHOD;
    node->return_type = return_type;
    node->name = strdup(name);
    node->params = NULL;
    node->param_count = 0;
    node->body = NULL;
    node->body_count = 0;
    return node;
}

FieldNode* make_field(Type field_type, char* name, int is_public) {
    FieldNode* node = malloc(sizeof(FieldNode));
    node->base.type = NODE_FIELD;
    node->field_type = field_type;
    node->name = strdup(name);
    node->is_public = is_public;
    return node;
}

LiteralNode* make_literal(char* value, int lit_type) {
    LiteralNode* node = malloc(sizeof(LiteralNode));
    node->base.type = NODE_LITERAL;
    node->value = strdup(value);
    node->lit_type = lit_type;
    return node;
}

IdentifierNode* make_identifier(char* name) {
    IdentifierNode* node = malloc(sizeof(IdentifierNode));
    node->base.type = NODE_IDENTIFIER;
    node->name = strdup(name);
    return node;
}

ReturnNode* make_return(ASTNode* value) {
    ReturnNode* node = malloc(sizeof(ReturnNode));
    node->base.type = NODE_RETURN;
    node->value = value;
    return node;
}

IfNode* make_if(ASTNode* condition) {
    IfNode* node = malloc(sizeof(IfNode));
    node->base.type = NODE_IF;
    node->condition = condition;
    node->then_body = NULL;
    node->then_count = 0;
    node->else_body = NULL;
    node->else_count = 0;
    return node;
}

WhileNode* make_while(ASTNode* condition) {
    WhileNode* node = malloc(sizeof(WhileNode));
    node->base.type = NODE_WHILE;
    node->condition = condition;
    node->body = NULL;
    node->body_count = 0;
    return node;
}

VarDeclNode* make_var_decl(Type type, char* name, ASTNode* init) {
    VarDeclNode* node = malloc(sizeof(VarDeclNode));
    node->base.type = NODE_VAR_DECL;
    node->var_type = type;
    node->var_name = strdup(name);
    node->initializer = init;
    return node;
}

AssignNode* make_assign(char* name, ASTNode* value) {
    AssignNode* node = malloc(sizeof(AssignNode));
    node->base.type = NODE_ASSIGN;
    node->var_name = strdup(name);
    node->value = value;
    return node;
}

BinaryOpNode* make_binary_op(char* op, ASTNode* left, ASTNode* right) {
    BinaryOpNode* node = malloc(sizeof(BinaryOpNode));
    node->base.type = NODE_BINARY_OP;
    node->op = strdup(op);
    node->left = left;
    node->right = right;
    return node;
}

MemberAccessNode* make_member_access(ASTNode* obj, char* member) {
    MemberAccessNode* node = malloc(sizeof(MemberAccessNode));
    node->base.type = NODE_MEMBER_ACCESS;
    node->object = obj;
    node->member = strdup(member);
    return node;
}

MethodCallNode* make_method_call(ASTNode* obj, char* method) {
    MethodCallNode* node = malloc(sizeof(MethodCallNode));
    node->base.type = NODE_METHOD_CALL;
    node->object = obj;
    node->method = strdup(method);
    node->args = NULL;
    node->arg_count = 0;
    return node;
}
