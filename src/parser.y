%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"
#include "codegen.h"

extern int yylex();
extern int yylineno;
void yyerror(const char* s);

ProgramNode* root = NULL;

// Helper to manage dynamic arrays
typedef struct NodeList {
    ASTNode** items;
    int count;
    int capacity;
} NodeList;

NodeList* list_new() {
    NodeList* l = malloc(sizeof(NodeList));
    l->items = NULL;
    l->count = 0;
    l->capacity = 0;
    return l;
}

void list_add(NodeList* l, ASTNode* node) {
    if (l->count >= l->capacity) {
        l->capacity = l->capacity == 0 ? 8 : l->capacity * 2;
        l->items = realloc(l->items, l->capacity * sizeof(ASTNode*));
    }
    l->items[l->count++] = node;
}
%}

%code requires {
    #include "ast.h"
}

%union {
    char* str;
    int num;
    ASTNode* node;
    ProgramNode* program;
    ClassNode* class_node;
    MethodNode* method_node;
    FieldNode* field_node;
    Type type_node;
    void* node_list;
}

%token INCLUDE CLASS PUBLIC PRIVATE IF ELSE RETURN NEW NIL
%token WHILE FOR IN BREAK CONTINUE SWITCH CASE DEFAULT
%token DEFER MATCH UNSAFE CONST STATIC EXTERN
%token INT STRING ERROR
%token LBRACE RBRACE LPAREN RPAREN SEMICOLON COMMA DOT QUESTION
%token ASSIGN DECLARE_ASSIGN LE EQ NE
%token CCPINCLUDE

%token <str> IDENTIFIER STRING_LITERAL
%token <num> NUMBER

%type <program> program
%type <class_node> class_decl
%type <method_node> method_decl
%type <field_node> field_decl
%type <type_node> type
%type <node> expr stmt return_stmt var_decl member_decl
%type <node_list> class_body stmt_list

%%

program:
    /* empty */           { 
        root = make_program(); 
        $$ = root; 
    }
    | program include_stmt  { 
        $$ = $1; 
    }
    | program ccpinclude_stmt  { 
        $$ = $1; 
    }
    | program class_decl  { 
        $$ = $1;
        $$->declarations = realloc($$->declarations, ($$->decl_count + 1) * sizeof(ASTNode*));
        $$->declarations[$$->decl_count++] = (ASTNode*)$2;
        printf("Added class: %s\n", $2->name);
    }
    ;

include_stmt:
    INCLUDE LBRACE include_list RBRACE
    | INCLUDE LBRACE include_list RBRACE SEMICOLON
    ;

ccpinclude_stmt:
    CCPINCLUDE STRING_LITERAL SEMICOLON  {
        char* header = $2;
        if (header[0] == '"') {
            header = strdup(header + 1);
            header[strlen(header) - 1] = '\0';
        }
        printf("C++ include: %s\n", header);
    }
    | CCPINCLUDE STRING_LITERAL  {
        char* header = $2;
        if (header[0] == '"') {
            header = strdup(header + 1);
            header[strlen(header) - 1] = '\0';
        }
        printf("C++ include: %s\n", header);
    }
    ;

include_list:
    include_path
    | include_list COMMA include_path
    ;

include_path:
    IDENTIFIER
    | include_path DOT IDENTIFIER
    ;

class_decl:
    PUBLIC CLASS IDENTIFIER LBRACE class_body RBRACE  { 
        $$ = make_class($3, 1);
        NodeList* body = (NodeList*)$5;
        for (int i = 0; i < body->count; i++) {
            ASTNode* member = body->items[i];
            if (member->type == NODE_FIELD) {
                $$->fields = realloc($$->fields, ($$->field_count + 1) * sizeof(FieldNode*));
                $$->fields[$$->field_count++] = (FieldNode*)member;
            } else if (member->type == NODE_METHOD) {
                $$->methods = realloc($$->methods, ($$->method_count + 1) * sizeof(MethodNode*));
                $$->methods[$$->method_count++] = (MethodNode*)member;
            }
        }
        free(body->items);
        free(body);
    }
    | CLASS IDENTIFIER LBRACE class_body RBRACE  { 
        $$ = make_class($2, 0);
        NodeList* body = (NodeList*)$4;
        for (int i = 0; i < body->count; i++) {
            ASTNode* member = body->items[i];
            if (member->type == NODE_FIELD) {
                $$->fields = realloc($$->fields, ($$->field_count + 1) * sizeof(FieldNode*));
                $$->fields[$$->field_count++] = (FieldNode*)member;
            } else if (member->type == NODE_METHOD) {
                $$->methods = realloc($$->methods, ($$->method_count + 1) * sizeof(MethodNode*));
                $$->methods[$$->method_count++] = (MethodNode*)member;
            }
        }
        free(body->items);
        free(body);
    }
    | PUBLIC CLASS IDENTIFIER LBRACE RBRACE  { 
        $$ = make_class($3, 1);
    }
    | CLASS IDENTIFIER LBRACE RBRACE  { 
        $$ = make_class($2, 0);
    }
    ;

class_body:
    member_decl           { 
        NodeList* list = list_new();
        list_add(list, $1);
        $$ = list;
    }
    | class_body member_decl  { 
        NodeList* list = (NodeList*)$1;
        list_add(list, $2);
        $$ = list;
    }
    ;

member_decl:
    field_decl  { $$ = (ASTNode*)$1; }
    | method_decl  { $$ = (ASTNode*)$1; }
    ;

field_decl:
    PRIVATE type IDENTIFIER SEMICOLON  { 
        $$ = make_field($2, $3, 0);
        printf("  Field: %s %s (private)\n", $2.name, $3);
    }
    | PUBLIC type IDENTIFIER SEMICOLON  { 
        $$ = make_field($2, $3, 1);
        printf("  Field: %s %s (public)\n", $2.name, $3);
    }
    | type IDENTIFIER SEMICOLON  { 
        $$ = make_field($1, $2, 0);
        printf("  Field: %s %s\n", $1.name, $2);
    }
    ;

method_decl:
    type IDENTIFIER LPAREN RPAREN LBRACE stmt_list RBRACE  {
        $$ = make_method($1, $2);
        NodeList* stmts = (NodeList*)$6;
        $$->body = stmts->items;
        $$->body_count = stmts->count;
        free(stmts);
        printf("  Method: %s %s() with %d statements\n", $1.name, $2, $$->body_count);
    }
    | type QUESTION IDENTIFIER LPAREN RPAREN LBRACE stmt_list RBRACE  {
        Type t = $1;
        t.nullable = 1;
        $$ = make_method(t, $3);
        NodeList* stmts = (NodeList*)$7;
        $$->body = stmts->items;
        $$->body_count = stmts->count;
        free(stmts);
        printf("  Method: %s? %s() with %d statements\n", $1.name, $3, $$->body_count);
    }
    ;

type:
    INT        { $$.name = strdup("int"); $$.nullable = 0; }
    | STRING   { $$.name = strdup("str"); $$.nullable = 0; }
    | ERROR    { $$.name = strdup("Error"); $$.nullable = 0; }
    | IDENTIFIER { $$.name = $1; $$.nullable = 0; }
    ;

stmt_list:
    /* empty */        { 
        $$ = list_new();
    }
    | stmt_list stmt   { 
        NodeList* list = (NodeList*)$1;
        if ($2) list_add(list, $2);
        $$ = list;
    }
    ;

stmt:
    var_decl
    | return_stmt
    | expr SEMICOLON  { $$ = $1; }
    | IDENTIFIER ASSIGN expr SEMICOLON  { 
        $$ = (ASTNode*)make_assign($1, $3);
    }
    | IF LPAREN expr RPAREN LBRACE stmt_list RBRACE  {
        IfNode* ifn = make_if($3);
        NodeList* stmts = (NodeList*)$6;
        ifn->then_body = stmts->items;
        ifn->then_count = stmts->count;
        free(stmts);
        $$ = (ASTNode*)ifn;
    }
    | IF LPAREN expr RPAREN LBRACE stmt_list RBRACE ELSE LBRACE stmt_list RBRACE  {
        IfNode* ifn = make_if($3);
        NodeList* then_stmts = (NodeList*)$6;
        NodeList* else_stmts = (NodeList*)$10;
        ifn->then_body = then_stmts->items;
        ifn->then_count = then_stmts->count;
        ifn->else_body = else_stmts->items;
        ifn->else_count = else_stmts->count;
        free(then_stmts);
        free(else_stmts);
        $$ = (ASTNode*)ifn;
    }
    | WHILE LPAREN expr RPAREN LBRACE stmt_list RBRACE  {
        WhileNode* wn = make_while($3);
        NodeList* stmts = (NodeList*)$6;
        wn->body = stmts->items;
        wn->body_count = stmts->count;
        free(stmts);
        $$ = (ASTNode*)wn;
    }
    ;

var_decl:
    type IDENTIFIER ASSIGN expr SEMICOLON  { 
        $$ = (ASTNode*)make_var_decl($1, $2, $4);
    }
    | type IDENTIFIER SEMICOLON  { 
        $$ = (ASTNode*)make_var_decl($1, $2, NULL);
    }
    ;

return_stmt:
    RETURN expr SEMICOLON  { 
        $$ = (ASTNode*)make_return($2);
    }
    | RETURN SEMICOLON  { 
        $$ = (ASTNode*)make_return(NULL);
    }
    ;

expr:
    NUMBER  { 
        char buf[32];
        sprintf(buf, "%d", $1);
        $$ = (ASTNode*)make_literal(buf, LIT_INT);
    }
    | STRING_LITERAL  { 
        $$ = (ASTNode*)make_literal($1, LIT_STRING);
    }
    | IDENTIFIER  { 
        $$ = (ASTNode*)make_identifier($1);
    }
    | NIL  { 
        $$ = (ASTNode*)make_literal("nil", LIT_NIL);
    }
    | NEW IDENTIFIER LPAREN RPAREN  { 
        $$ = (ASTNode*)make_identifier($2);
    }
    | expr DOT IDENTIFIER  { 
        $$ = (ASTNode*)make_member_access($1, $3);
    }
    | expr DOT IDENTIFIER LPAREN RPAREN  {
        $$ = (ASTNode*)make_method_call($1, $3);
    }
    | expr LE expr  {
        $$ = (ASTNode*)make_binary_op("<=", $1, $3);
    }
    | expr EQ expr  {
        $$ = (ASTNode*)make_binary_op("==", $1, $3);
    }
    | expr NE expr  {
        $$ = (ASTNode*)make_binary_op("!=", $1, $3);
    }
    | LPAREN expr RPAREN  {
        $$ = $2;
    }
    ;

%%

void yyerror(const char* s) {
    fprintf(stderr, "Error line %d: %s\n", yylineno, s);
}

int main() {
    int result = yyparse();
    if (result == 0 && root) {
        printf("\nParse successful! %d declarations\n", root->decl_count);
        
        // Generate C++ code
        FILE* out = fopen("output.cpp", "w");
        if (out) {
            codegen(root, out);
            fclose(out);
            printf("Generated output.cpp\n");
        }
    }
    return result;
}
