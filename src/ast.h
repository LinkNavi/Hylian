#ifndef AST_H
#define AST_H

typedef enum {
    NODE_PROGRAM,
    NODE_INCLUDE,
 NODE_CPP_INCLUDE,
    NODE_CLASS,
    NODE_METHOD,
    NODE_FIELD,
    NODE_VAR_DECL,
    NODE_RETURN,
    NODE_BINARY_OP,
    NODE_LITERAL,
    NODE_IDENTIFIER,
    NODE_MEMBER_ACCESS,
    NODE_METHOD_CALL,
    NODE_IF,
    NODE_WHILE,
    NODE_ASSIGN,
} NodeType;

typedef struct ASTNode {
  NodeType type;
} ASTNode;

typedef struct {
  char *name;
  int nullable;
} Type;

typedef struct {
  ASTNode base;
  char **paths;
  int path_count;
} IncludeNode;

typedef struct {
  ASTNode base;
  Type field_type;
  char *name;
  int is_public;
} FieldNode;

typedef struct {
  ASTNode base;
  Type return_type;
  char *name;
  ASTNode **params;
  int param_count;
  ASTNode **body;
  int body_count;
} MethodNode;

typedef struct {
  ASTNode base;
  ASTNode *condition;
  ASTNode **then_body;
  int then_count;
  ASTNode **else_body;
  int else_count;
} IfNode;

typedef struct {
  ASTNode base;
  ASTNode *condition;
  ASTNode **body;
  int body_count;
} WhileNode;

typedef struct {
  ASTNode base;
  Type var_type;
  char *var_name;
  ASTNode *initializer;
} VarDeclNode;

typedef struct {
  ASTNode base;
  char *var_name;
  ASTNode *value;
} AssignNode;

typedef struct {
  ASTNode base;
  char *op; // "<=", "==", "!=", etc
  ASTNode *left;
  ASTNode *right;
} BinaryOpNode;

typedef struct {
  ASTNode base;
  ASTNode *object;
  char *member;
} MemberAccessNode;

typedef struct {
  ASTNode base;
  ASTNode *object;
  char *method;
  ASTNode **args;
  int arg_count;
} MethodCallNode;
typedef struct {
  ASTNode base;
  char *name;
  int is_public;
  FieldNode **fields;
  int field_count;
  MethodNode **methods;
  int method_count;
} ClassNode;

typedef struct {
  ASTNode base;
  ASTNode **declarations;
  int decl_count;
} ProgramNode;

typedef struct {
  ASTNode base;
  char *value;
  enum { LIT_INT, LIT_STRING, LIT_NIL } lit_type;
} LiteralNode;

typedef struct {
  ASTNode base;
  char *name;
} IdentifierNode;
typedef struct {
    ASTNode base;
    char* header;  // The C++ header to include (e.g., "vector", "iostream")
} CppIncludeNode;
typedef struct {
  ASTNode base;
  ASTNode *value;
} ReturnNode;

// Constructor functions
ProgramNode *make_program();
ClassNode *make_class(char *name, int is_public);
MethodNode *make_method(Type return_type, char *name);
FieldNode *make_field(Type field_type, char *name, int is_public);
LiteralNode *make_literal(char *value, int lit_type);
IdentifierNode *make_identifier(char *name);
ReturnNode *make_return(ASTNode *value);
IfNode* make_if(ASTNode* condition);
WhileNode* make_while(ASTNode* condition);
VarDeclNode* make_var_decl(Type type, char* name, ASTNode* init);
AssignNode* make_assign(char* name, ASTNode* value);
BinaryOpNode* make_binary_op(char* op, ASTNode* left, ASTNode* right);
CppIncludeNode* make_cpp_include(char* header);
MemberAccessNode* make_member_access(ASTNode* obj, char* member);
MethodCallNode* make_method_call(ASTNode* obj, char* method);
#endif
