%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"


extern int yylex();
extern int yylineno;
void yyerror(const char* s);

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

%union {
    char* str;
    int num;
    ASTNode* node;
    ProgramNode* program;
    ClassNode* class_node;
    MethodNode* method_node;
    FuncNode* func_node;
    FieldNode* field_node;
    Type type_node;
    void* node_list;
}

%token INCLUDE CLASS PUBLIC PRIVATE IF ELSE RETURN NEW NIL TRUE_LIT FALSE_LIT
%token WHILE FOR IN BREAK CONTINUE SWITCH CASE DEFAULT
%token DEFER MATCH UNSAFE CONST STATIC EXTERN AMP
%token INT STRING ERROR BOOL
%token <str> ASM_BLOCK
%token LBRACE RBRACE LPAREN RPAREN LBRACKET RBRACKET SEMICOLON COMMA DOT QUESTION
%token ASSIGN DECLARE_ASSIGN
%token INC DEC
%token PLUS_ASSIGN MINUS_ASSIGN STAR_ASSIGN SLASH_ASSIGN MOD_ASSIGN
%token AND OR NOT
%token GE LE EQ NE GT LT
%token PLUS MINUS STAR SLASH MOD
%token CCPINCLUDE VOID
%token ARRAY MULTI ANY_KW PIPE
%token <str> IDENTIFIER STRING_LITERAL FLOAT_LITERAL INTERP_STRING
%token <num> NUMBER

%type <program> program
%type <class_node> class_decl
%type <method_node> method_decl
%type <func_node> func_decl
%type <field_node> field_decl
%type <type_node> type
%type <node> expr stmt return_stmt var_decl member_decl for_init
%type <node_list> class_body stmt_list param_list params arg_list args union_types include_list
%type <node> include_path


/* Operator precedence, lowest to highest */
%right ASSIGN PLUS_ASSIGN MINUS_ASSIGN STAR_ASSIGN SLASH_ASSIGN MOD_ASSIGN
%left OR
%left AND
%left EQ NE
%left LT GT LE GE
%left PLUS MINUS
%left STAR SLASH MOD
%right NOT UMINUS
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
                $$->methods = realloc($$->methods, ($$->method_count+1)*sizeof(MethodNode*));
                $$->methods[$$->method_count++] = (MethodNode*)m;
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
                $$->methods = realloc($$->methods, ($$->method_count+1)*sizeof(MethodNode*));
                $$->methods[$$->method_count++] = (MethodNode*)m;
            }
        }
        free(body->items); free(body);
    }
    | PUBLIC CLASS IDENTIFIER LBRACE RBRACE { $$ = make_class($3, 1); }
    | CLASS IDENTIFIER LBRACE RBRACE        { $$ = make_class($2, 0); }
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
    ;

class_body:
    member_decl { NodeList* l=list_new(); list_add(l,$1); $$=l; }
    | class_body member_decl { NodeList* l=(NodeList*)$1; list_add(l,$2); $$=l; }
    ;

member_decl:
    field_decl  { $$ = (ASTNode*)$1; }
    | method_decl { $$ = (ASTNode*)$1; }
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
    | BREAK SEMICOLON    { $$ = (ASTNode*)make_break(); }
    | CONTINUE SEMICOLON { $$ = (ASTNode*)make_continue(); }
    | ASM_BLOCK {
        AsmBlockNode *ab = malloc(sizeof(AsmBlockNode));
        ab->base.type = NODE_ASM_BLOCK;
        ab->body = $1;
        $$ = (ASTNode*)ab;
    }
    | DEFER expr SEMICOLON { $$ = (ASTNode*)make_defer($2); }
    | expr SEMICOLON { $$ = $1; }
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
    | FOR LPAREN for_init SEMICOLON expr SEMICOLON expr RPAREN LBRACE stmt_list RBRACE {
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
    | RETURN SEMICOLON    { $$ = (ASTNode*)make_return(NULL); }
    ;

expr:
    NUMBER {
        char buf[32]; sprintf(buf, "%d", $1);
        $$ = (ASTNode*)make_literal(buf, LIT_INT);
    }
    | FLOAT_LITERAL { $$ = (ASTNode*)make_literal($1, LIT_FLOAT); }
    | STRING_LITERAL { $$ = (ASTNode*)make_literal($1, LIT_STRING); }
    | INTERP_STRING  { $$ = (ASTNode*)make_interp_string($1); }
    | NIL    { $$ = (ASTNode*)make_literal("nullptr", LIT_NIL); }
    | TRUE_LIT  { $$ = (ASTNode*)make_literal("true", LIT_BOOL); }
    | FALSE_LIT { $$ = (ASTNode*)make_literal("false", LIT_BOOL); }
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
    /* unary */
    | NOT expr          { $$ = (ASTNode*)make_unary_op("!", $2, 0); }
    | MINUS expr %prec UMINUS { $$ = (ASTNode*)make_unary_op("-", $2, 0); }
    | INC expr          { $$ = (ASTNode*)make_unary_op("++", $2, 0); }
    | DEC expr          { $$ = (ASTNode*)make_unary_op("--", $2, 0); }
    | expr INC          { $$ = (ASTNode*)make_unary_op("++", $1, 1); }
    | expr DEC          { $$ = (ASTNode*)make_unary_op("--", $1, 1); }
    | LPAREN expr RPAREN { $$ = $2; }
    ;

%%
