#ifndef AST_H
#define AST_H

typedef enum {
    NODE_PROGRAM,
    NODE_INCLUDE,
    NODE_CPP_INCLUDE,
    NODE_CLASS,
    NODE_METHOD,
    NODE_FUNC,
    NODE_FIELD,
    NODE_VAR_DECL,
    NODE_RETURN,
    NODE_BINARY_OP,
    NODE_UNARY_OP,
    NODE_LITERAL,
    NODE_IDENTIFIER,
    NODE_MEMBER_ACCESS,
    NODE_MEMBER_ASSIGN,
    NODE_METHOD_CALL,
    NODE_FUNC_CALL,
    NODE_NEW,
    NODE_IF,
    NODE_WHILE,
    NODE_FOR,
    NODE_FOR_IN,
    NODE_BREAK,
    NODE_CONTINUE,
    NODE_ASSIGN,
    NODE_COMPOUND_ASSIGN,
    NODE_DEFER,
    NODE_INTERP_STRING,
    NODE_ARRAY_LITERAL,
    NODE_INDEX,
    NODE_INDEX_ASSIGN,
    NODE_ASM_BLOCK,
    NODE_TUPLE,     /* tuple literal: (a, b, c) */
    NODE_ENUM,      /* enum Color { Red, Green, Blue } */
    NODE_SWITCH,    /* switch (expr) { case v: { } ... default: { } } */
    NODE_CASE,      /* one arm of a switch: case value: { body } or default: { body } */
} NodeType;

typedef enum {
    TYPE_SIMPLE,   /* int, str, bool, void, Error, ClassName */
    TYPE_ARRAY,    /* array<T> or array<T, N> */
    TYPE_MULTI,    /* multi<A | B | ...> or multi<any> with optional N */
    TYPE_TUPLE,    /* (A, B, ...) — tuple return type */
} TypeKind;

typedef struct Type {
    TypeKind kind;
    int nullable;
    /* TYPE_SIMPLE */
    char *name;
    /* TYPE_ARRAY / TYPE_MULTI */
    struct Type *elem_types;  /* array of element types */
    int elem_type_count;
    int is_any;               /* multi<any> */
    int fixed_size;           /* 0 = flexible, >0 = fixed */
} Type;

typedef struct ASTNode { NodeType type; Type resolved_type; } ASTNode;

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
    Type return_type;
    char *name;
    ASTNode **params;
    int param_count;
    ASTNode **body;
    int body_count;
} FuncNode;

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
    ASTNode *init;
    ASTNode *condition;
    ASTNode *post;
    ASTNode **body;
    int body_count;
} ForNode;

typedef struct {
    ASTNode base;
    char *var_name;      /* loop variable name, e.g. "item" */
    int   use_ref;       /* 1 = auto&, 0 = auto (copy) */
    ASTNode *collection; /* the expression being iterated */
    ASTNode **body;
    int body_count;
} ForInNode;

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
    char *op;
    char *var_name;
    ASTNode *value;
} CompoundAssignNode;

typedef struct {
    ASTNode base;
    char *op;
    ASTNode *left;
    ASTNode *right;
} BinaryOpNode;

typedef struct {
    ASTNode base;
    char *op;
    ASTNode *operand;
    int postfix;
} UnaryOpNode;

typedef struct {
    ASTNode base;
    ASTNode *object;
    char *member;
} MemberAccessNode;

typedef struct {
    ASTNode base;
    ASTNode *object; /* NULL = this/implicit */
    char *member;
    ASTNode *value;
} MemberAssignNode;

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
    ASTNode **args;
    int arg_count;
} FuncCallNode;

typedef struct {
    ASTNode base;
    char *class_name;
    ASTNode **args;
    int arg_count;
} NewNode;

typedef struct {
    ASTNode base;
    char *name;
    int is_public;
    FieldNode **fields;
    int field_count;
    MethodNode **methods;
    int method_count;
    ASTNode **ctor_params;
    int ctor_param_count;
    ASTNode **ctor_body;
    int ctor_body_count;
    int has_ctor;
} ClassNode;

typedef struct {
    char *name;   /* variant identifier, e.g. "Red" */
    int   value;  /* integer value, e.g. 0 */
} EnumVariant;

typedef struct {
    ASTNode base;
    char *name;
    int is_public;
    EnumVariant *variants;
    int variant_count;
} EnumNode;

typedef struct {
    ASTNode base;
    ASTNode **declarations;
    int decl_count;
    char **includes;
    int include_count;
    char **cpp_includes;
    int cpp_include_count;
} ProgramNode;

typedef struct {
    ASTNode base;
    char *value;
    enum { LIT_INT, LIT_STRING, LIT_NIL, LIT_BOOL, LIT_FLOAT } lit_type;
} LiteralNode;

typedef struct { ASTNode base; char *name; } IdentifierNode;

/* Each segment of an interpolated string: either a literal piece or an expr source */
typedef struct {
    int is_expr;   /* 0 = literal text, 1 = expression source */
    char *text;    /* literal text (no quotes) OR raw expression source */
} InterpSegment;

typedef struct {
    ASTNode base;
    InterpSegment *segments;
    int seg_count;
} InterpStringNode;

typedef struct { ASTNode base; char *header; } CppIncludeNode;
typedef struct { ASTNode base; ASTNode *value; } ReturnNode;

typedef struct {
    ASTNode base;
    ASTNode **elements;
    int elem_count;
} TupleNode;
typedef struct { ASTNode base; ASTNode *expr; } DeferNode;
typedef struct { ASTNode base; } BreakNode;
typedef struct { ASTNode base; } ContinueNode;
typedef struct { ASTNode base; char *body; } AsmBlockNode;

typedef struct {
    ASTNode base;
    ASTNode **elements;
    int elem_count;
} ArrayLiteralNode;

typedef struct {
    ASTNode base;
    ASTNode *object;
    ASTNode *index;
} IndexNode;

typedef struct {
    ASTNode base;
    ASTNode *object;
    ASTNode *index;
    ASTNode *value;
} IndexAssignNode;

ProgramNode *make_program();
EnumNode *make_enum(char *name, int is_public);
ClassNode *make_class(char *name, int is_public);
MethodNode *make_method(Type return_type, char *name);
FuncNode *make_func(Type return_type, char *name);
FieldNode *make_field(Type field_type, char *name, int is_public);
LiteralNode *make_literal(char *value, int lit_type);
IdentifierNode *make_identifier(char *name);
ReturnNode *make_return(ASTNode *value);
IfNode *make_if(ASTNode *condition);
WhileNode *make_while(ASTNode *condition);
ForNode *make_for(ASTNode *init, ASTNode *condition, ASTNode *post);
ForInNode *make_for_in(char *var_name, int use_ref, ASTNode *collection);
VarDeclNode *make_var_decl(Type type, char *name, ASTNode *init);
AssignNode *make_assign(char *name, ASTNode *value);
CompoundAssignNode *make_compound_assign(char *op, char *name, ASTNode *value);
BinaryOpNode *make_binary_op(char *op, ASTNode *left, ASTNode *right);
UnaryOpNode *make_unary_op(char *op, ASTNode *operand, int postfix);
CppIncludeNode *make_cpp_include(char *header);
MemberAccessNode *make_member_access(ASTNode *obj, char *member);
MemberAssignNode *make_member_assign(ASTNode *obj, char *member, ASTNode *value);
MethodCallNode *make_method_call(ASTNode *obj, char *method);
FuncCallNode *make_func_call(char *name);
NewNode *make_new(char *class_name);
InterpStringNode *make_interp_string(const char *raw); /* raw = full string with quotes */
DeferNode *make_defer(ASTNode *expr);
BreakNode *make_break();
ContinueNode *make_continue();

typedef struct {
    ASTNode base;
    ASTNode *value;   /* NULL for default arm */
    int is_default;
    ASTNode **body;
    int body_count;
} SwitchCaseNode;

typedef struct {
    ASTNode base;
    ASTNode *subject;          /* the expression being switched on */
    SwitchCaseNode **cases;    /* array of case arms (includes default if present) */
    int case_count;
} SwitchNode;

SwitchCaseNode *make_switch_case(ASTNode *value, int is_default);
SwitchNode *make_switch(ASTNode *subject);
ArrayLiteralNode *make_array_literal(ASTNode **elems, int count);
IndexNode *make_index(ASTNode *object, ASTNode *index);
IndexAssignNode *make_index_assign(ASTNode *object, ASTNode *index, ASTNode *value);
TupleNode *make_tuple(ASTNode **elems, int count);
Type make_simple_type(char *name, int nullable);
Type make_array_type(Type elem, int fixed_size);
Type make_multi_type(Type *elems, int count, int is_any, int fixed_size);
Type make_tuple_type(Type *elems, int count);

#endif