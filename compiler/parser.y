%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"

extern int yylex();
extern int yylineno;

/* Set this to the current source file path before calling yyparse() */
const char *current_parse_file = "<unknown>";
const char *current_compile_target = "linux";

void yyerror(const char *s) {
    /* Bold red "error:" label */
    fprintf(stderr, "\033[1m%s:%d:\033[0m \033[1;31merror:\033[0m %s\n",
            current_parse_file, yylineno, s);

    /* Hint logic — inspect the Bison-generated message for common patterns */
    const char *hint = NULL;

    if (strstr(s, "unexpected '}'") || strstr(s, "unexpected RBRACE")) {
        hint = "check for a missing \033[1;36m;\033[0m before the closing \033[1;36m}\033[0m";
    } else if (strstr(s, "unexpected '{'") || strstr(s, "unexpected LBRACE")) {
        hint = "check for a missing \033[1;36m;\033[0m or mismatched braces";
    } else if (strstr(s, "unexpected IDENTIFIER")) {
        hint = "an identifier appeared where it wasn't expected — did you forget a \033[1;36m:\033[0m type annotation, a \033[1;36m,\033[0m, or a \033[1;36m;\033[0m?";
    } else if (strstr(s, "unexpected ';'") || strstr(s, "unexpected SEMICOLON")) {
        hint = "extra or misplaced \033[1;36m;\033[0m — check for an empty statement or a stray semicolon";
    } else if (strstr(s, "unexpected '('") || strstr(s, "unexpected LPAREN")) {
        hint = "unexpected \033[1;36m(\033[0m — did you forget the function name or a type annotation?";
    } else if (strstr(s, "unexpected ')'") || strstr(s, "unexpected RPAREN")) {
        hint = "unexpected \033[1;36m)\033[0m — check for mismatched parentheses or a missing argument";
    } else if (strstr(s, "unexpected ','") || strstr(s, "unexpected COMMA")) {
        hint = "unexpected \033[1;36m,\033[0m — check for a trailing comma or a missing expression";
    } else if (strstr(s, "unexpected $end") || strstr(s, "unexpected end of input") || strstr(s, "unexpected end of file")) {
        hint = "the file ended unexpectedly — check for unclosed \033[1;36m{\033[0m blocks or a missing \033[1;36m}\033[0m";
    } else if (strstr(s, "unexpected ASSIGN") || strstr(s, "unexpected '='")) {
        hint = "unexpected \033[1;36m=\033[0m — did you mean \033[1;36m:=\033[0m for declaration-assignment, or is a type missing?";
    } else if (strstr(s, "unexpected RETURN")) {
        hint = "misplaced \033[1;36mreturn\033[0m — is it outside a function body?";
    } else if (strstr(s, "unexpected IF") || strstr(s, "unexpected WHILE") || strstr(s, "unexpected FOR")) {
        hint = "control-flow keyword in an unexpected position — check surrounding braces and semicolons";
    } else if (strstr(s, "unexpected NEW")) {
        hint = "unexpected \033[1;36mnew\033[0m — did you forget an \033[1;36m=\033[0m or \033[1;36m:=\033[0m before it?";
    } else if (strstr(s, "unexpected CLASS")) {
        hint = "unexpected \033[1;36mclass\033[0m — class declarations must appear at the top level";
    } else if (strstr(s, "unexpected PIPE") || strstr(s, "unexpected '|'")) {
        hint = "unexpected \033[1;36m|\033[0m — pipe types are only valid inside \033[1;36mmulti<...>\033[0m";
    } else if (strstr(s, "unexpected NUMBER") || strstr(s, "unexpected STRING_LITERAL")) {
        hint = "a literal appeared where it wasn't expected — check for a missing operator or \033[1;36m;\033[0m";
    } else if (strstr(s, "syntax error")) {
        hint = "double-check the statement for missing semicolons, mismatched brackets, or incorrect type syntax";
    }

    if (hint) {
        fprintf(stderr, "  \033[1;33mhint:\033[0m %s\n", hint);
    }
}

ProgramNode* root = NULL;

typedef struct NodeList {
    ASTNode** items;
    int count;
    int capacity;
} NodeList;

NodeList* list_new() {
    NodeList* l = malloc(sizeof(NodeList));
    l->items = NULL; l->count = 0; l->capacity = 0;
    return l;
}

void list_add(NodeList* l, ASTNode* node) {
    if (l->count >= l->capacity) {
        l->capacity = l->capacity == 0 ? 8 : l->capacity * 2;
        l->items = realloc(l->items, l->capacity * sizeof(ASTNode*));
    }
    l->items[l->count++] = node;
}

/* TypeList: a growable array of Type values for union_types */
typedef struct TypeList {
    Type *items;
    int count;
    int capacity;
} TypeList;

TypeList* typelist_new() {
    TypeList* l = malloc(sizeof(TypeList));
    l->items = NULL; l->count = 0; l->capacity = 0;
    return l;
}

void typelist_add(TypeList* l, Type t) {
    if (l->count >= l->capacity) {
        l->capacity = l->capacity == 0 ? 4 : l->capacity * 2;
        l->items = realloc(l->items, l->capacity * sizeof(Type));
    }
    l->items[l->count++] = t;
}
%}

%code requires { #include "ast.h" }

%define parse.error verbose

%union {
    char* str;
    long num;
    ASTNode* node;
    ProgramNode* program;
    ClassNode* class_node;
    EnumNode* enum_node;
    MethodNode* method_node;
    FuncNode* func_node;
    FieldNode* field_node;
    ModuleNode* module_node;
    Type type_node;
    void* node_list;
}

%token INCLUDE CLASS ENUM PUBLIC PRIVATE IF ELSE RETURN NEW NIL TRUE_LIT FALSE_LIT
%token WHILE FOR IN BREAK CONTINUE SWITCH CASE DEFAULT
%token DEFER UNSAFE CONST STATIC EXTERN AMP
%token INT STRING ERROR BOOL
%token VOLATILE PACKED NAKED USIZE ISIZE UNION_KW
%token TILDE LSHIFT RSHIFT XOR CAST SIZE_OF AS
%token MODULE
%token <str> ASM_BLOCK
%token LBRACE RBRACE LPAREN RPAREN LBRACKET RBRACKET SEMICOLON COMMA DOT QUESTION COLON
%token ASSIGN DECLARE_ASSIGN
%token INC DEC
%token PLUS_ASSIGN MINUS_ASSIGN STAR_ASSIGN SLASH_ASSIGN MOD_ASSIGN
%token AND OR NOT
%token GE LE EQ NE GT LT
%token ADDROF_FN
%token PLUS MINUS STAR SLASH MOD
%token CCPINCLUDE VOID
%token ARRAY MULTI ANY_KW PIPE
%token <str> IDENTIFIER STRING_LITERAL FLOAT_LITERAL INTERP_STRING
%token <num> NUMBER

%type <program> program
%type <node> static_var_decl
%type <class_node> class_decl
%type <module_node> module_decl
%type <node_list> module_body
%type <node> module_member
%type <enum_node> enum_decl enum_body
%type <method_node> method_decl
%type <func_node> func_decl
%type <field_node> field_decl
%type <type_node> type nullable_type
%type <node> expr stmt return_stmt var_decl member_decl for_init ctor_decl
%type <node_list> class_body stmt_list param_list params arg_list args union_types include_list tuple_type_items switch_arms field_init_list
%type <node> include_path


/* Operator precedence, lowest to highest */
%right ASSIGN PLUS_ASSIGN MINUS_ASSIGN STAR_ASSIGN SLASH_ASSIGN MOD_ASSIGN
%left OR
%left AND
%left PIPE
%left XOR
%left AMP
%left EQ NE
%left LT GT LE GE
%left LSHIFT RSHIFT
%left PLUS MINUS
%left STAR SLASH MOD
%right NOT TILDE UMINUS
%left AS
%left INC DEC
%left DOT
%left LBRACKET

%%

program:
    /* empty */ { root = make_program(); $$ = root; }
    | program include_stmt      { $$ = $1; }
    | program ccpinclude_stmt   { $$ = $1; }
    | program class_decl {
        $$ = $1;
        $$->declarations = realloc($$->declarations, ($$->decl_count+1)*sizeof(ASTNode*));
        $$->declarations[$$->decl_count++] = (ASTNode*)$2;
    }
    | program func_decl {
        $$ = $1;
        $$->declarations = realloc($$->declarations, ($$->decl_count+1)*sizeof(ASTNode*));
        $$->declarations[$$->decl_count++] = (ASTNode*)$2;
    }
    | program enum_decl {
        $$ = $1;
        $$->declarations = realloc($$->declarations, ($$->decl_count+1)*sizeof(ASTNode*));
        $$->declarations[$$->decl_count++] = (ASTNode*)$2;
    }
    | program static_var_decl {
        $$ = $1;
        $$->declarations = realloc($$->declarations, ($$->decl_count+1)*sizeof(ASTNode*));
        $$->declarations[$$->decl_count++] = (ASTNode*)$2;
    }
    | program module_decl {
        $$ = $1;
        $$->declarations = realloc($$->declarations, ($$->decl_count+1)*sizeof(ASTNode*));
        $$->declarations[$$->decl_count++] = (ASTNode*)$2;
    }
    ;

include_stmt:
    INCLUDE LBRACE include_list RBRACE {
        NodeList* l = (NodeList*)$3;
        for (int i = 0; i < l->count; i++) {
            IdentifierNode* path = (IdentifierNode*)l->items[i];
            root->includes = realloc(root->includes, (root->include_count+1)*sizeof(char*));
            root->includes[root->include_count++] = path->name;
        }
        free(l->items); free(l);
    }
    | INCLUDE LBRACE include_list RBRACE SEMICOLON {
        NodeList* l = (NodeList*)$3;
        for (int i = 0; i < l->count; i++) {
            IdentifierNode* path = (IdentifierNode*)l->items[i];
            root->includes = realloc(root->includes, (root->include_count+1)*sizeof(char*));
            root->includes[root->include_count++] = path->name;
        }
        free(l->items); free(l);
    }
    ;

ccpinclude_stmt:
    CCPINCLUDE STRING_LITERAL SEMICOLON {
        /* strip surrounding quotes from the string literal */
        int len = strlen($2);
        char *stripped = malloc(len - 1);
        strncpy(stripped, $2 + 1, len - 2);
        stripped[len - 2] = '\0';
        root->cpp_includes = realloc(root->cpp_includes, (root->cpp_include_count+1)*sizeof(char*));
        root->cpp_includes[root->cpp_include_count++] = stripped;
        free($2);
    }
    | CCPINCLUDE STRING_LITERAL {
        int len = strlen($2);
        char *stripped = malloc(len - 1);
        strncpy(stripped, $2 + 1, len - 2);
        stripped[len - 2] = '\0';
        root->cpp_includes = realloc(root->cpp_includes, (root->cpp_include_count+1)*sizeof(char*));
        root->cpp_includes[root->cpp_include_count++] = stripped;
        free($2);
    }
    ;

include_list:
    include_path                                { NodeList* l=list_new(); list_add(l,$1); $$=l; }
    | include_list COMMA include_path           { NodeList* l=(NodeList*)$1; list_add(l,$3); $$=l; }
    | include_list COMMA                        { $$ = $1; }
    ;

include_path:
    IDENTIFIER {
        /* build path string incrementally using an IdentifierNode to carry it */
        $$ = (ASTNode*)make_identifier($1);
    }
    | include_path DOT IDENTIFIER {
        /* append .IDENTIFIER to the accumulated path string */
        IdentifierNode* prev = (IdentifierNode*)$1;
        int len = strlen(prev->name) + 1 + strlen($3) + 1;
        char* combined = malloc(len);
        snprintf(combined, len, "%s.%s", prev->name, $3);
        free(prev->name);
        prev->name = combined;
        $$ = (ASTNode*)prev;
    }
    ;

static_var_decl:
    STATIC type IDENTIFIER ASSIGN expr SEMICOLON {
        StaticVarNode *sv = malloc(sizeof(StaticVarNode));
        sv->base.type = NODE_STATIC_VAR;
        memset(&sv->base.resolved_type, 0, sizeof(Type));
        sv->var_type = $2;
        sv->var_name = $3;
        sv->initializer = $5;
        sv->is_const = 0; sv->array_size = 0;
        $$ = (ASTNode*)sv;
    }
    | STATIC type IDENTIFIER SEMICOLON {
        StaticVarNode *sv = malloc(sizeof(StaticVarNode));
        sv->base.type = NODE_STATIC_VAR;
        memset(&sv->base.resolved_type, 0, sizeof(Type));
        sv->var_type = $2;
        sv->var_name = $3;
        sv->initializer = NULL;
        sv->is_const = 0; sv->array_size = 0;
        $$ = (ASTNode*)sv;
    }
    | PUBLIC STATIC type IDENTIFIER ASSIGN expr SEMICOLON {
        StaticVarNode *sv = malloc(sizeof(StaticVarNode));
        sv->base.type = NODE_STATIC_VAR;
        memset(&sv->base.resolved_type, 0, sizeof(Type));
        sv->var_type = $3;
        sv->var_name = $4;
        sv->initializer = $6;
        sv->is_const = 0; sv->array_size = 0;
        $$ = (ASTNode*)sv;
    }
    | PUBLIC STATIC type IDENTIFIER SEMICOLON {
        StaticVarNode *sv = malloc(sizeof(StaticVarNode));
        sv->base.type = NODE_STATIC_VAR;
        memset(&sv->base.resolved_type, 0, sizeof(Type));
        sv->var_type = $3;
        sv->var_name = $4;
        sv->initializer = NULL;
        sv->is_const = 0; sv->array_size = 0;
        $$ = (ASTNode*)sv;
    }
    | CONST type IDENTIFIER ASSIGN expr SEMICOLON {
        StaticVarNode *sv = malloc(sizeof(StaticVarNode));
        sv->base.type = NODE_STATIC_VAR;
        memset(&sv->base.resolved_type, 0, sizeof(Type));
        sv->var_type = $2; sv->var_name = $3; sv->initializer = $5;
        sv->is_const = 1; sv->array_size = 0;
        $$ = (ASTNode*)sv;
    }
    | PUBLIC CONST type IDENTIFIER ASSIGN expr SEMICOLON {
        StaticVarNode *sv = malloc(sizeof(StaticVarNode));
        sv->base.type = NODE_STATIC_VAR;
        memset(&sv->base.resolved_type, 0, sizeof(Type));
        sv->var_type = $3; sv->var_name = $4; sv->initializer = $6;
        sv->is_const = 1; sv->array_size = 0;
        $$ = (ASTNode*)sv;
    }
    | STATIC type IDENTIFIER LBRACKET NUMBER RBRACKET SEMICOLON {
        StaticVarNode *sv = malloc(sizeof(StaticVarNode));
        sv->base.type = NODE_STATIC_VAR;
        memset(&sv->base.resolved_type, 0, sizeof(Type));
        sv->var_type = $2; sv->var_name = $3;
        sv->initializer = NULL; sv->is_const = 0; sv->array_size = (int)$5;
        $$ = (ASTNode*)sv;
    }
    | PUBLIC STATIC type IDENTIFIER LBRACKET NUMBER RBRACKET SEMICOLON {
        StaticVarNode *sv = malloc(sizeof(StaticVarNode));
        sv->base.type = NODE_STATIC_VAR;
        memset(&sv->base.resolved_type, 0, sizeof(Type));
        sv->var_type = $3; sv->var_name = $4;
        sv->initializer = NULL; sv->is_const = 0; sv->array_size = (int)$6;
        $$ = (ASTNode*)sv;
    }
    ;


class_decl:
    PUBLIC CLASS IDENTIFIER LBRACE class_body RBRACE {
        $$ = make_class($3, 1);
        NodeList* body = (NodeList*)$5;
        for (int i = 0; i < body->count; i++) {
            ASTNode* m = body->items[i];
            if (m->type == NODE_FIELD) {
                $$->fields = realloc($$->fields, ($$->field_count+1)*sizeof(FieldNode*));
                $$->fields[$$->field_count++] = (FieldNode*)m;
            } else if (m->type == NODE_METHOD) {
                MethodNode* mn = (MethodNode*)m;
                if (mn->return_type.name && strcmp(mn->return_type.name, "__ctor__") == 0) {
                    $$->ctor_params = mn->params; $$->ctor_param_count = mn->param_count;
                    $$->ctor_body = mn->body; $$->ctor_body_count = mn->body_count;
                    $$->has_ctor = 1;
                } else {
                    $$->methods = realloc($$->methods, ($$->method_count+1)*sizeof(MethodNode*));
                    $$->methods[$$->method_count++] = mn;
                }
            }
        }
        free(body->items); free(body);
    }
    | CLASS IDENTIFIER LBRACE class_body RBRACE {
        $$ = make_class($2, 0);
        NodeList* body = (NodeList*)$4;
        for (int i = 0; i < body->count; i++) {
            ASTNode* m = body->items[i];
            if (m->type == NODE_FIELD) {
                $$->fields = realloc($$->fields, ($$->field_count+1)*sizeof(FieldNode*));
                $$->fields[$$->field_count++] = (FieldNode*)m;
            } else if (m->type == NODE_METHOD) {
                MethodNode* mn = (MethodNode*)m;
                if (mn->return_type.name && strcmp(mn->return_type.name, "__ctor__") == 0) {
                    $$->ctor_params = mn->params; $$->ctor_param_count = mn->param_count;
                    $$->ctor_body = mn->body; $$->ctor_body_count = mn->body_count;
                    $$->has_ctor = 1;
                } else {
                    $$->methods = realloc($$->methods, ($$->method_count+1)*sizeof(MethodNode*));
                    $$->methods[$$->method_count++] = mn;
                }
            }
        }
        free(body->items); free(body);
    }
    | PUBLIC CLASS IDENTIFIER LBRACE RBRACE { $$ = make_class($3, 1); }
    | CLASS IDENTIFIER LBRACE RBRACE        { $$ = make_class($2, 0); }
    /* packed variants */
    | PUBLIC PACKED CLASS IDENTIFIER LBRACE class_body RBRACE {
        $$ = make_class($4, 1);
        $$->is_packed = 1;
        NodeList* body = (NodeList*)$6;
        for (int i = 0; i < body->count; i++) {
            ASTNode* m = body->items[i];
            if (m->type == NODE_FIELD) {
                $$->fields = realloc($$->fields, ($$->field_count+1)*sizeof(FieldNode*));
                $$->fields[$$->field_count++] = (FieldNode*)m;
            } else if (m->type == NODE_METHOD) {
                MethodNode* mn = (MethodNode*)m;
                if (mn->return_type.name && strcmp(mn->return_type.name, "__ctor__") == 0) {
                    $$->ctor_params = mn->params; $$->ctor_param_count = mn->param_count;
                    $$->ctor_body = mn->body; $$->ctor_body_count = mn->body_count;
                    $$->has_ctor = 1;
                } else {
                    $$->methods = realloc($$->methods, ($$->method_count+1)*sizeof(MethodNode*));
                    $$->methods[$$->method_count++] = mn;
                }
            }
        }
        free(body->items); free(body);
    }
    | PACKED CLASS IDENTIFIER LBRACE class_body RBRACE {
        $$ = make_class($3, 0);
        $$->is_packed = 1;
        NodeList* body = (NodeList*)$5;
        for (int i = 0; i < body->count; i++) {
            ASTNode* m = body->items[i];
            if (m->type == NODE_FIELD) {
                $$->fields = realloc($$->fields, ($$->field_count+1)*sizeof(FieldNode*));
                $$->fields[$$->field_count++] = (FieldNode*)m;
            } else if (m->type == NODE_METHOD) {
                MethodNode* mn = (MethodNode*)m;
                if (mn->return_type.name && strcmp(mn->return_type.name, "__ctor__") == 0) {
                    $$->ctor_params = mn->params; $$->ctor_param_count = mn->param_count;
                    $$->ctor_body = mn->body; $$->ctor_body_count = mn->body_count;
                    $$->has_ctor = 1;
                } else {
                    $$->methods = realloc($$->methods, ($$->method_count+1)*sizeof(MethodNode*));
                    $$->methods[$$->method_count++] = mn;
                }
            }
        }
        free(body->items); free(body);
    }
    | PUBLIC PACKED CLASS IDENTIFIER LBRACE RBRACE { $$ = make_class($4, 1); $$->is_packed = 1; }
    | PACKED CLASS IDENTIFIER LBRACE RBRACE        { $$ = make_class($3, 0); $$->is_packed = 1; }
    /* union variants: all fields share offset 0, size = max field size */
    | UNION_KW CLASS IDENTIFIER LBRACE class_body RBRACE {
        $$ = make_class($3, 0);
        $$->is_union = 1;
        NodeList* body = (NodeList*)$5;
        for (int i = 0; i < body->count; i++) {
            ASTNode* m = body->items[i];
            if (m->type == NODE_FIELD) {
                $$->fields = realloc($$->fields, ($$->field_count+1)*sizeof(FieldNode*));
                $$->fields[$$->field_count++] = (FieldNode*)m;
            }
        }
        free(body->items); free(body);
    }
    | PUBLIC UNION_KW CLASS IDENTIFIER LBRACE class_body RBRACE {
        $$ = make_class($4, 1);
        $$->is_union = 1;
        NodeList* body = (NodeList*)$6;
        for (int i = 0; i < body->count; i++) {
            ASTNode* m = body->items[i];
            if (m->type == NODE_FIELD) {
                $$->fields = realloc($$->fields, ($$->field_count+1)*sizeof(FieldNode*));
                $$->fields[$$->field_count++] = (FieldNode*)m;
            }
        }
        free(body->items); free(body);
    }
    | UNION_KW CLASS IDENTIFIER LBRACE RBRACE        { $$ = make_class($3, 0); $$->is_union = 1; }
    | PUBLIC UNION_KW CLASS IDENTIFIER LBRACE RBRACE { $$ = make_class($4, 1); $$->is_union = 1; }
    /* Constructor variant: class Foo { Foo(params) { ... } fields/methods } */
    | PUBLIC CLASS IDENTIFIER LBRACE IDENTIFIER LPAREN param_list RPAREN LBRACE stmt_list RBRACE class_body RBRACE {
        $$ = make_class($3, 1);
        if ($7) {
            NodeList* pl = (NodeList*)$7;
            $$->ctor_params = pl->items; $$->ctor_param_count = pl->count; free(pl);
        }
        NodeList* cb = (NodeList*)$10;
        $$->ctor_body = cb->items; $$->ctor_body_count = cb->count; free(cb);
        $$->has_ctor = 1;
        NodeList* body = (NodeList*)$12;
        for (int i = 0; i < body->count; i++) {
            ASTNode* m = body->items[i];
            if (m->type == NODE_FIELD) {
                $$->fields = realloc($$->fields, ($$->field_count+1)*sizeof(FieldNode*));
                $$->fields[$$->field_count++] = (FieldNode*)m;
            } else if (m->type == NODE_METHOD) {
                $$->methods = realloc($$->methods, ($$->method_count+1)*sizeof(MethodNode*));
                $$->methods[$$->method_count++] = (MethodNode*)m;
            }
        }
        free(body->items); free(body);
    }
    | CLASS IDENTIFIER LBRACE IDENTIFIER LPAREN param_list RPAREN LBRACE stmt_list RBRACE class_body RBRACE {
        $$ = make_class($2, 0);
        if ($6) {
            NodeList* pl = (NodeList*)$6;
            $$->ctor_params = pl->items; $$->ctor_param_count = pl->count; free(pl);
        }
        NodeList* cb = (NodeList*)$9;
        $$->ctor_body = cb->items; $$->ctor_body_count = cb->count; free(cb);
        $$->has_ctor = 1;
        NodeList* body = (NodeList*)$11;
        for (int i = 0; i < body->count; i++) {
            ASTNode* m = body->items[i];
            if (m->type == NODE_FIELD) {
                $$->fields = realloc($$->fields, ($$->field_count+1)*sizeof(FieldNode*));
                $$->fields[$$->field_count++] = (FieldNode*)m;
            } else if (m->type == NODE_METHOD) {
                $$->methods = realloc($$->methods, ($$->method_count+1)*sizeof(MethodNode*));
                $$->methods[$$->method_count++] = (MethodNode*)m;
            }
        }
        free(body->items); free(body);
    }
    ;

enum_decl:
    ENUM IDENTIFIER LBRACE enum_body RBRACE {
        free($4->name);
        $4->name = strdup($2); $4->is_public = 0;
        free($2); $$ = $4;
    }
    | PUBLIC ENUM IDENTIFIER LBRACE enum_body RBRACE {
        free($5->name);
        $5->name = strdup($3); $5->is_public = 1;
        free($3); $$ = $5;
    }
    ;

enum_body:
    IDENTIFIER {
        EnumNode *en = make_enum("__enum_tmp__", 0);
        en->variants = realloc(en->variants, sizeof(EnumVariant));
        en->variants[0].name  = strdup($1);
        en->variants[0].value = 0;
        en->variant_count = 1;
        free($1); $$ = en;
    }
    | enum_body COMMA IDENTIFIER {
        EnumNode *en = $1;
        int next_val = en->variant_count;
        en->variants = realloc(en->variants,
                               (en->variant_count + 1) * sizeof(EnumVariant));
        en->variants[en->variant_count].name  = strdup($3);
        en->variants[en->variant_count].value = next_val;
        en->variant_count++;
        free($3); $$ = en;
    }
    | enum_body COMMA { $$ = $1; }
    | IDENTIFIER ASSIGN NUMBER {
        EnumNode *en = make_enum("__enum_tmp__", 0);
        en->variants = realloc(en->variants, sizeof(EnumVariant));
        en->variants[0].name  = strdup($1);
        en->variants[0].value = (int)$3;
        en->variant_count = 1;
        free($1); $$ = en;
    }
    | enum_body COMMA IDENTIFIER ASSIGN NUMBER {
        EnumNode *en = $1;
        en->variants = realloc(en->variants, (en->variant_count + 1) * sizeof(EnumVariant));
        en->variants[en->variant_count].name  = strdup($3);
        en->variants[en->variant_count].value = (int)$5;
        en->variant_count++;
        free($3); $$ = en;
    }
    ;

module_decl:
    MODULE IDENTIFIER LBRACE module_body RBRACE {
        $$ = make_module($2);
        free($2);
        NodeList *body = (NodeList*)$4;
        for (int i = 0; i < body->count; i++) {
            ASTNode *m = body->items[i];
            if (!m) continue;
            if (m->type == NODE_FUNC) {
                FuncNode *fn = (FuncNode *)m;
                $$->funcs = realloc($$->funcs, ($$->func_count + 1) * sizeof(FuncNode *));
                $$->func_is_public = realloc($$->func_is_public, ($$->func_count + 1) * sizeof(int));
                $$->func_is_public[$$->func_count] = fn->is_public;
                fn->module_name = strdup($$->name);
                $$->funcs[$$->func_count++] = fn;
            } else if (m->type == NODE_STATIC_VAR) {
                StaticVarNode *sv = (StaticVarNode *)m;
                $$->statics = realloc($$->statics, ($$->static_count + 1) * sizeof(StaticVarNode *));
                $$->statics[$$->static_count++] = sv;
            }
        }
        free(body->items); free(body);
    }
    ;

module_body:
    /* empty */ { $$ = list_new(); }
    | module_body module_member {
        NodeList *l = (NodeList*)$1;
        if ($2) list_add(l, $2);
        $$ = l;
    }
    ;

module_member:
    PUBLIC func_decl {
        $2->is_public = 1;
        $$ = (ASTNode*)$2;
    }
    | func_decl {
        $1->is_public = 0;
        $$ = (ASTNode*)$1;
    }
    | static_var_decl { $$ = $1; }
    ;

func_decl:
    type IDENTIFIER LPAREN param_list RPAREN LBRACE stmt_list RBRACE {
        $$ = make_func($1, $2);
        if ($4) { NodeList* pl=(NodeList*)$4; $$->params=pl->items; $$->param_count=pl->count; free(pl); }
        NodeList* sl=(NodeList*)$7; $$->body=sl->items; $$->body_count=sl->count; free(sl);
    }
    | type QUESTION IDENTIFIER LPAREN param_list RPAREN LBRACE stmt_list RBRACE {
        Type t=$1; t.nullable=1;
        $$ = make_func(t, $3);
        if ($5) { NodeList* pl=(NodeList*)$5; $$->params=pl->items; $$->param_count=pl->count; free(pl); }
        NodeList* sl=(NodeList*)$8; $$->body=sl->items; $$->body_count=sl->count; free(sl);
    }
    /* naked variants */
    | NAKED type IDENTIFIER LPAREN param_list RPAREN LBRACE stmt_list RBRACE {
        $$ = make_func($2, $3);
        $$->is_naked = 1;
        if ($5) { NodeList* pl=(NodeList*)$5; $$->params=pl->items; $$->param_count=pl->count; free(pl); }
        NodeList* sl=(NodeList*)$8; $$->body=sl->items; $$->body_count=sl->count; free(sl);
    }
    /* error recovery: a bad function signature — skip to the closing brace */
    | error LBRACE stmt_list RBRACE {
        $$ = make_func(make_simple_type("void", 0), "<error>");
        yyerrok;
    }
    ;

class_body:
    member_decl { NodeList* l=list_new(); if ($1) list_add(l,$1); $$=l; }
    | class_body member_decl { NodeList* l=(NodeList*)$1; if ($2) list_add(l,$2); $$=l; }
    ;

member_decl:
    field_decl  { $$ = (ASTNode*)$1; }
    | method_decl { $$ = (ASTNode*)$1; }
    | ctor_decl { $$ = (ASTNode*)$1; }
    /* error recovery: skip a malformed member up to the next '}' or ';' */
    | error SEMICOLON { $$ = NULL; yyerrok; }
    ;

ctor_decl:
    IDENTIFIER LPAREN param_list RPAREN LBRACE stmt_list RBRACE {
        Type void_t; memset(&void_t, 0, sizeof(void_t)); void_t.kind = TYPE_SIMPLE; void_t.name = strdup("__ctor__");
        MethodNode* mn = make_method(void_t, $1);
        if ($3) { NodeList* pl=(NodeList*)$3; mn->params=pl->items; mn->param_count=pl->count; free(pl); }
        NodeList* sl=(NodeList*)$6; mn->body=sl->items; mn->body_count=sl->count; free(sl);
        $$ = (ASTNode*)mn;
    }
    ;

field_decl:
    PRIVATE type IDENTIFIER SEMICOLON { $$ = make_field($2, $3, 0); }
    | PUBLIC type IDENTIFIER SEMICOLON  { $$ = make_field($2, $3, 1); }
    | type IDENTIFIER SEMICOLON         { $$ = make_field($1, $2, 0); }
    ;

method_decl:
    type IDENTIFIER LPAREN param_list RPAREN LBRACE stmt_list RBRACE {
        $$ = make_method($1, $2);
        if ($4) { NodeList* pl=(NodeList*)$4; $$->params=pl->items; $$->param_count=pl->count; free(pl); }
        NodeList* sl=(NodeList*)$7; $$->body=sl->items; $$->body_count=sl->count; free(sl);
    }
    | type QUESTION IDENTIFIER LPAREN param_list RPAREN LBRACE stmt_list RBRACE {
        Type t=$1; t.nullable=1;
        $$ = make_method(t, $3);
        if ($5) { NodeList* pl=(NodeList*)$5; $$->params=pl->items; $$->param_count=pl->count; free(pl); }
        NodeList* sl=(NodeList*)$8; $$->body=sl->items; $$->body_count=sl->count; free(sl);
    }
    | NAKED type IDENTIFIER LPAREN param_list RPAREN LBRACE stmt_list RBRACE {
        $$ = make_method($2, $3);
        $$->is_naked = 1;
        if ($5) { NodeList* pl=(NodeList*)$5; $$->params=pl->items; $$->param_count=pl->count; free(pl); }
        NodeList* sl=(NodeList*)$8; $$->body=sl->items; $$->body_count=sl->count; free(sl);
    }
    ;

param_list:
    /* empty */ { $$ = NULL; }
    | params    { $$ = $1; }
    ;

params:
    type IDENTIFIER {
        NodeList* l=list_new();
        list_add(l,(ASTNode*)make_var_decl($1,$2,NULL));
        $$=l;
    }
    | params COMMA type IDENTIFIER {
        NodeList* l=(NodeList*)$1;
        list_add(l,(ASTNode*)make_var_decl($3,$4,NULL));
        $$=l;
    }
    ;

arg_list:
    /* empty */ { $$ = list_new(); }
    | args      { $$ = $1; }
    ;

args:
    expr { NodeList* l=list_new(); list_add(l,$1); $$=l; }
    | args COMMA expr { NodeList* l=(NodeList*)$1; list_add(l,$3); $$=l; }
    ;

/* nullable_type: a type with an optional trailing '?' */
nullable_type:
    type              { $$ = $1; }
    | type QUESTION   { Type t = $1; t.nullable = 1; $$ = t; }
    ;

/* tuple_type_items: at least two nullable_type separated by commas, stored as TypeList* */
tuple_type_items:
    nullable_type COMMA nullable_type {
        TypeList* tl = typelist_new();
        typelist_add(tl, $1);
        typelist_add(tl, $3);
        $$ = (void*)tl;
    }
    | tuple_type_items COMMA nullable_type {
        TypeList* tl = (TypeList*)$1;
        typelist_add(tl, $3);
        $$ = (void*)tl;
    }
    ;

/* union_types: pipe-separated list of types for multi<A | B | ...>
   We reuse void* node_list slot but store a TypeList* instead */
union_types:
    type {
        TypeList* tl = typelist_new();
        typelist_add(tl, $1);
        $$ = (void*)tl;
    }
    | union_types PIPE type {
        TypeList* tl = (TypeList*)$1;
        typelist_add(tl, $3);
        $$ = (void*)tl;
    }
    ;

type:
    INT        { $$ = make_simple_type("int",   0); }
    | STRING   { $$ = make_simple_type("str",   0); }
    | ERROR    { $$ = make_simple_type("Error", 0); }
    | VOID     { $$ = make_simple_type("void",  0); }
    | BOOL     { $$ = make_simple_type("bool",  0); }
    | USIZE    { $$ = make_simple_type("usize", 0); }
    | ISIZE    { $$ = make_simple_type("isize", 0); }
| STAR type  { $$ = make_rawptr_type($2); }
| AMP  type  { $$ = make_ref_type($2); }
    | IDENTIFIER { $$ = make_simple_type($1,    0); free($1); }
    /* array<T> */
    | ARRAY LT type GT                      { $$ = make_array_type($3, 0); }
    /* array<T, N> */
    | ARRAY LT type COMMA NUMBER GT         { $$ = make_array_type($3, $5); }
    /* multi<any> */
    | MULTI LT ANY_KW GT                    { $$ = make_multi_type(NULL, 0, 1, 0); }
    /* multi<any, N> */
    | MULTI LT ANY_KW COMMA NUMBER GT       { $$ = make_multi_type(NULL, 0, 1, $5); }
    /* multi<A | B | ...> */
    | MULTI LT union_types GT {
        TypeList* tl = (TypeList*)$3;
        $$ = make_multi_type(tl->items, tl->count, 0, 0);
        free(tl->items); free(tl);
    }
    /* multi<A | B | ..., N> */
    | MULTI LT union_types COMMA NUMBER GT {
        TypeList* tl = (TypeList*)$3;
        $$ = make_multi_type(tl->items, tl->count, 0, $5);
        free(tl->items); free(tl);
    }
    /* tuple type: (A, B) or (A?, B, C) etc. */
    | LPAREN tuple_type_items RPAREN {
        TypeList* tl = (TypeList*)$2;
        $$ = make_tuple_type(tl->items, tl->count);
        free(tl->items); free(tl);
    }
    ;

stmt_list:
    /* empty */ { $$ = list_new(); }
    | stmt_list stmt {
        NodeList* l=(NodeList*)$1;
        if ($2) list_add(l,$2);
        $$=l;
    }
    ;

stmt:
    var_decl
    | return_stmt
    /* function-local static: `static int x = 0;` or `static int buf[N];` */
    | static_var_decl { $$ = $1; }
    /* error recovery: skip a malformed statement up to the next ';' */
    | error SEMICOLON { $$ = NULL; yyerrok; }
    | BREAK SEMICOLON    { $$ = (ASTNode*)make_break(); }
    | CONTINUE SEMICOLON { $$ = (ASTNode*)make_continue(); }
    | ASM_BLOCK {
        AsmBlockNode *ab = malloc(sizeof(AsmBlockNode));
        ab->base.type = NODE_ASM_BLOCK;
        ab->base.resolved_type.kind = TYPE_SIMPLE;
        ab->base.resolved_type.nullable = 0;
        ab->base.resolved_type.name = NULL;
        ab->base.resolved_type.elem_types = NULL;
        ab->base.resolved_type.elem_type_count = 0;
        ab->base.resolved_type.is_any = 0;
        ab->base.resolved_type.fixed_size = 0;
        ab->body = $1;
        $$ = (ASTNode*)ab;
    }
| UNSAFE LBRACE stmt_list RBRACE {
    NodeList *sl = (NodeList*)$3;
    $$ = (ASTNode*)make_unsafe_block(sl->items, sl->count);
    free(sl);
}
    | DEFER expr SEMICOLON { $$ = (ASTNode*)make_defer($2); }
    | expr SEMICOLON { $$ = $1; }
    /* volatile write: volatile *lhs = rhs; */
    | VOLATILE STAR expr ASSIGN expr SEMICOLON {
        UnaryOpNode *deref = (UnaryOpNode*)make_unary_op("deref", $3, 0);
        deref->is_volatile = 1;
        /* Represent as a NODE_ASSIGN-like structure using member_assign trick.
           We store it as a special unary deref on the lhs wrapped in an assign.
           Use a BinaryOpNode with op "volatile_store", lhs=ptr, rhs=value. */
        BinaryOpNode *vs = (BinaryOpNode*)make_binary_op("volatile_store", $3, $5);
        $$ = (ASTNode*)vs;
    }
    /* plain deref-write: *ptr = val; */
    | STAR expr ASSIGN expr SEMICOLON {
        BinaryOpNode *ds = (BinaryOpNode*)make_binary_op("deref_store", $2, $4);
        $$ = (ASTNode*)ds;
    }
    /* := declare-assign (type inferred as auto) */
    | IDENTIFIER DECLARE_ASSIGN expr SEMICOLON {
        $$ = (ASTNode*)make_var_decl(make_simple_type("auto", 0), $1, $3);
    }
    /* simple assign */
    | IDENTIFIER ASSIGN expr SEMICOLON {
        $$ = (ASTNode*)make_assign($1, $3);
    }
    /* compound assign */
    | IDENTIFIER PLUS_ASSIGN expr SEMICOLON  { $$ = (ASTNode*)make_compound_assign("+=", $1, $3); }
    | IDENTIFIER MINUS_ASSIGN expr SEMICOLON { $$ = (ASTNode*)make_compound_assign("-=", $1, $3); }
    | IDENTIFIER STAR_ASSIGN expr SEMICOLON  { $$ = (ASTNode*)make_compound_assign("*=", $1, $3); }
    | IDENTIFIER SLASH_ASSIGN expr SEMICOLON { $$ = (ASTNode*)make_compound_assign("/=", $1, $3); }
    | IDENTIFIER MOD_ASSIGN expr SEMICOLON   { $$ = (ASTNode*)make_compound_assign("%=", $1, $3); }
    /* member assign: obj.field = expr */
    | expr DOT IDENTIFIER ASSIGN expr SEMICOLON {
        $$ = (ASTNode*)make_member_assign($1, $3, $5);
    }
    /* index assign: expr[expr] = expr */
    | expr LBRACKET expr RBRACKET ASSIGN expr SEMICOLON {
        $$ = (ASTNode*)make_index_assign($1, $3, $6);
    }
    /* if */
    | IF LPAREN expr RPAREN LBRACE stmt_list RBRACE {
        IfNode* ifn = make_if($3);
        NodeList* ts=(NodeList*)$6; ifn->then_body=ts->items; ifn->then_count=ts->count; free(ts);
        $$ = (ASTNode*)ifn;
    }
    | IF LPAREN expr RPAREN LBRACE stmt_list RBRACE ELSE LBRACE stmt_list RBRACE {
        IfNode* ifn = make_if($3);
        NodeList* ts=(NodeList*)$6; NodeList* es=(NodeList*)$10;
        ifn->then_body=ts->items; ifn->then_count=ts->count;
        ifn->else_body=es->items; ifn->else_count=es->count;
        free(ts); free(es);
        $$ = (ASTNode*)ifn;
    }
    /* while */
    | WHILE LPAREN expr RPAREN LBRACE stmt_list RBRACE {
        WhileNode* wn = make_while($3);
        NodeList* sl=(NodeList*)$6; wn->body=sl->items; wn->body_count=sl->count; free(sl);
        $$ = (ASTNode*)wn;
    }
    /* for */
    | FOR LPAREN for_init SEMICOLON expr SEMICOLON for_init RPAREN LBRACE stmt_list RBRACE {
        ForNode* fn = make_for($3, $5, $7);
        NodeList* sl=(NodeList*)$10; fn->body=sl->items; fn->body_count=sl->count; free(sl);
        $$ = (ASTNode*)fn;
    }
    | FOR LPAREN for_init SEMICOLON expr SEMICOLON RPAREN LBRACE stmt_list RBRACE {
        ForNode* fn = make_for($3, $5, NULL);
        NodeList* sl=(NodeList*)$9; fn->body=sl->items; fn->body_count=sl->count; free(sl);
        $$ = (ASTNode*)fn;
    }
    /* for-in: for (item in collection) */
    | FOR LPAREN IDENTIFIER IN expr RPAREN LBRACE stmt_list RBRACE {
        ForInNode* fn = make_for_in($3, 0, $5);
        NodeList* sl=(NodeList*)$8; fn->body=sl->items; fn->body_count=sl->count; free(sl);
        free($3);
        $$ = (ASTNode*)fn;
    }
    /* for-in by reference: for (&item in collection) */
    | FOR LPAREN AMP IDENTIFIER IN expr RPAREN LBRACE stmt_list RBRACE {
        ForInNode* fn = make_for_in($4, 1, $6);
        NodeList* sl=(NodeList*)$9; fn->body=sl->items; fn->body_count=sl->count; free(sl);
        free($4);
        $$ = (ASTNode*)fn;
    }
    /* switch */
    | SWITCH LPAREN expr RPAREN LBRACE switch_arms RBRACE {
        SwitchNode* sn = make_switch($3);
        NodeList* arms = (NodeList*)$6;
        sn->cases = (SwitchCaseNode**)arms->items;
        sn->case_count = arms->count;
        free(arms);
        $$ = (ASTNode*)sn;
    }
    ;

switch_arms:
    /* empty */ { $$ = list_new(); }
    | switch_arms CASE expr COLON LBRACE stmt_list RBRACE {
        NodeList* l = (NodeList*)$1;
        SwitchCaseNode* c = make_switch_case($3, 0);
        NodeList* body = (NodeList*)$6; c->body = body->items; c->body_count = body->count; free(body);
        list_add(l, (ASTNode*)c); $$ = l;
    }
    | switch_arms DEFAULT COLON LBRACE stmt_list RBRACE {
        NodeList* l = (NodeList*)$1;
        SwitchCaseNode* c = make_switch_case(NULL, 1);
        NodeList* body = (NodeList*)$5; c->body = body->items; c->body_count = body->count; free(body);
        list_add(l, (ASTNode*)c); $$ = l;
    }
    ;

for_init:
    type IDENTIFIER ASSIGN expr { $$ = (ASTNode*)make_var_decl($1, $2, $4); }
    | type IDENTIFIER           { $$ = (ASTNode*)make_var_decl($1, $2, NULL); }
    | IDENTIFIER ASSIGN expr    { $$ = (ASTNode*)make_assign($1, $3); }
    | /* empty */               { $$ = NULL; }
    ;

var_decl:
    type IDENTIFIER ASSIGN expr SEMICOLON { $$ = (ASTNode*)make_var_decl($1, $2, $4); }
    | type IDENTIFIER SEMICOLON           { $$ = (ASTNode*)make_var_decl($1, $2, NULL); }
    | type QUESTION IDENTIFIER ASSIGN expr SEMICOLON {
        Type t = $1; t.nullable = 1;
        $$ = (ASTNode*)make_var_decl(t, $3, $5);
    }
    | type QUESTION IDENTIFIER SEMICOLON {
        Type t = $1; t.nullable = 1;
        $$ = (ASTNode*)make_var_decl(t, $3, NULL);
    }
    ;

return_stmt:
    RETURN expr SEMICOLON { $$ = (ASTNode*)make_return($2); }
    | RETURN expr COMMA args SEMICOLON {
        NodeList* al = (NodeList*)$4;
        int total = 1 + al->count;
        ASTNode **elems = malloc(total * sizeof(ASTNode*));
        elems[0] = $2;
        for (int i = 0; i < al->count; i++) elems[i+1] = al->items[i];
        free(al->items); free(al);
        ASTNode* tup = (ASTNode*)make_tuple(elems, total);
        free(elems);
        $$ = (ASTNode*)make_return(tup);
    }
    | RETURN SEMICOLON    { $$ = (ASTNode*)make_return(NULL); }
    ;

expr:
    NUMBER {
        char buf[32]; sprintf(buf, "%ld", $1);
        $$ = (ASTNode*)make_literal(buf, LIT_INT);
    }
    | FLOAT_LITERAL { $$ = (ASTNode*)make_literal($1, LIT_FLOAT); }
    | STRING_LITERAL { $$ = (ASTNode*)make_literal($1, LIT_STRING); }
    | INTERP_STRING  { $$ = (ASTNode*)make_interp_string($1); }
    | NIL    { $$ = (ASTNode*)make_literal("nullptr", LIT_NIL); }
    | TRUE_LIT  { $$ = (ASTNode*)make_literal("true", LIT_BOOL); }
    | FALSE_LIT { $$ = (ASTNode*)make_literal("false", LIT_BOOL); }
| AMP expr %prec UMINUS { $$ = (ASTNode*)make_unary_op("addrof", $2, 0); }
| STAR expr %prec UMINUS { $$ = (ASTNode*)make_unary_op("deref", $2, 0); }
| VOLATILE STAR expr %prec UMINUS {
    UnaryOpNode *u = (UnaryOpNode*)make_unary_op("deref", $3, 0);
    u->is_volatile = 1;
    $$ = (ASTNode*)u;
}
| ADDROF_FN LPAREN IDENTIFIER RPAREN {
    FuncCallNode *c = make_func_call("__addrof_fn__");
    c->args = malloc(sizeof(ASTNode*));
    c->args[0] = (ASTNode*)make_identifier($3);
    c->arg_count = 1;
    $$ = (ASTNode*)c;
}
    /* array literal */
    | LBRACKET arg_list RBRACKET {
        NodeList* al = (NodeList*)$2;
        $$ = (ASTNode*)make_array_literal(al->items, al->count);
        free(al->items); free(al);
    }
    /* index access */
    | expr LBRACKET expr RBRACKET { $$ = (ASTNode*)make_index($1, $3); }
    /* free function call */
    | IDENTIFIER LPAREN arg_list RPAREN {
        FuncCallNode* c = make_func_call($1);
        NodeList* al=(NodeList*)$3; c->args=al->items; c->arg_count=al->count; free(al);
        $$ = (ASTNode*)c;
    }
    /* new */
    | NEW IDENTIFIER LPAREN arg_list RPAREN {
        NewNode* n = make_new($2);
        NodeList* al=(NodeList*)$4; n->args=al->items; n->arg_count=al->count; free(al);
        $$ = (ASTNode*)n;
    }
    | IDENTIFIER { $$ = (ASTNode*)make_identifier($1); }
    /* method call */
    | expr DOT IDENTIFIER LPAREN arg_list RPAREN {
        MethodCallNode* c = make_method_call($1, $3);
        NodeList* al=(NodeList*)$5; c->args=al->items; c->arg_count=al->count; free(al);
        $$ = (ASTNode*)c;
    }
    /* member access */
    | expr DOT IDENTIFIER { $$ = (ASTNode*)make_member_access($1, $3); }
    /* binary ops */
    | expr PLUS  expr { $$ = (ASTNode*)make_binary_op("+",  $1, $3); }
    | expr MINUS expr { $$ = (ASTNode*)make_binary_op("-",  $1, $3); }
    | expr STAR  expr { $$ = (ASTNode*)make_binary_op("*",  $1, $3); }
    | expr SLASH expr { $$ = (ASTNode*)make_binary_op("/",  $1, $3); }
    | expr MOD   expr { $$ = (ASTNode*)make_binary_op("%",  $1, $3); }
    | expr LT    expr { $$ = (ASTNode*)make_binary_op("<",  $1, $3); }
    | expr GT    expr { $$ = (ASTNode*)make_binary_op(">",  $1, $3); }
    | expr LE    expr { $$ = (ASTNode*)make_binary_op("<=", $1, $3); }
    | expr GE    expr { $$ = (ASTNode*)make_binary_op(">=", $1, $3); }
    | expr EQ    expr { $$ = (ASTNode*)make_binary_op("==", $1, $3); }
    | expr NE    expr { $$ = (ASTNode*)make_binary_op("!=", $1, $3); }
    | expr AND   expr { $$ = (ASTNode*)make_binary_op("&&", $1, $3); }
    | expr OR    expr { $$ = (ASTNode*)make_binary_op("||", $1, $3); }
    /* bitwise binary */
    | expr AMP   expr %prec AMP  { $$ = (ASTNode*)make_binary_op("&",  $1, $3); }
    | expr PIPE  expr { $$ = (ASTNode*)make_binary_op("|",  $1, $3); }
    | expr XOR   expr { $$ = (ASTNode*)make_binary_op("^",  $1, $3); }
    | expr LSHIFT expr { $$ = (ASTNode*)make_binary_op("<<", $1, $3); }
    | expr RSHIFT expr { $$ = (ASTNode*)make_binary_op(">>", $1, $3); }
    /* bitwise unary */
    | TILDE expr      { $$ = (ASTNode*)make_unary_op("~", $2, 0); }
    /* `expr as Type` — postfix cast shorthand, same semantics as cast<T>(expr) */
    | expr AS type %prec AS {
        char *tname = $3.name ? strdup($3.name) : strdup("void");
        ASTNode *type_node = (ASTNode*)make_literal(tname, LIT_STRING);
        free(tname);
        $$ = (ASTNode*)make_binary_op("cast", $1, type_node);
    }
    /* cast */
    | CAST LT type GT LPAREN expr RPAREN {
        /* Represent as a unary op "cast" carrying the type name in a BinaryOpNode
           with a string literal as the right child (the type name). */
        char *tname = $3.name ? strdup($3.name) : strdup("void");
        ASTNode *type_node = (ASTNode*)make_literal(tname, LIT_STRING);
        free(tname);
        $$ = (ASTNode*)make_binary_op("cast", $6, type_node);
    }
    /* size_of */
    | SIZE_OF LPAREN IDENTIFIER RPAREN {
        FuncCallNode *c = make_func_call("__size_of__");
        c->args = malloc(sizeof(ASTNode*));
        c->args[0] = (ASTNode*)make_identifier($3);
        c->arg_count = 1;
        $$ = (ASTNode*)c;
    }
    /* unary */
    | NOT expr          { $$ = (ASTNode*)make_unary_op("!", $2, 0); }
    | MINUS expr %prec UMINUS { $$ = (ASTNode*)make_unary_op("-", $2, 0); }
    | INC expr          { $$ = (ASTNode*)make_unary_op("++", $2, 0); }
    | DEC expr          { $$ = (ASTNode*)make_unary_op("--", $2, 0); }
    | expr INC          { $$ = (ASTNode*)make_unary_op("++", $1, 1); }
    | expr DEC          { $$ = (ASTNode*)make_unary_op("--", $1, 1); }
    | LPAREN expr RPAREN { $$ = $2; }
    /* struct literal: ClassName { field: value, ... } */
    | IDENTIFIER LBRACE field_init_list RBRACE {
        StructLiteralNode *sl = malloc(sizeof(StructLiteralNode));
        sl->base.type = NODE_STRUCT_LITERAL;
        memset(&sl->base.resolved_type, 0, sizeof(Type));
        sl->class_name = $1;
        NodeList *fl = (NodeList*)$3;
        sl->field_count  = fl->count / 2;
        sl->field_names  = malloc(sl->field_count * sizeof(char*));
        sl->field_values = malloc(sl->field_count * sizeof(ASTNode*));
        for (int i = 0; i < sl->field_count; i++) {
            sl->field_names[i]  = ((IdentifierNode*)fl->items[i*2])->name;
            sl->field_values[i] = fl->items[i*2+1];
        }
        free(fl->items); free(fl);
        $$ = (ASTNode*)sl;
    }
    /* tuple literal: (a, b) or (a, b, c, ...) */
    | LPAREN expr COMMA args RPAREN {
        NodeList* al = (NodeList*)$4;
        int total = 1 + al->count;
        ASTNode **elems = malloc(total * sizeof(ASTNode*));
        elems[0] = $2;
        for (int i = 0; i < al->count; i++) elems[i+1] = al->items[i];
        free(al->items); free(al);
        $$ = (ASTNode*)make_tuple(elems, total);
        free(elems);
    }
    ;

field_init_list:
    /* empty */ { $$ = list_new(); }
    | IDENTIFIER COLON expr {
        NodeList *l = list_new();
        list_add(l, (ASTNode*)make_identifier($1));
        list_add(l, $3);
        $$ = l;
    }
    | field_init_list COMMA IDENTIFIER COLON expr {
        NodeList *l = (NodeList*)$1;
        list_add(l, (ASTNode*)make_identifier($3));
        list_add(l, $5);
        $$ = l;
    }
    ;

%%
