%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int yylex();
extern int yylineno;
void yyerror(const char* s);
%}

%union {
    char* str;
    int num;
    void* node;
}

%token INCLUDE CLASS PUBLIC PRIVATE IF ELSE RETURN NEW NIL
%token WHILE FOR IN BREAK CONTINUE SWITCH CASE DEFAULT
%token DEFER MATCH UNSAFE CONST STATIC EXTERN
%token INT STRING ERROR
%token LBRACE RBRACE LPAREN RPAREN SEMICOLON COMMA DOT QUESTION
%token ASSIGN DECLARE_ASSIGN LE EQ NE

%token <str> IDENTIFIER STRING_LITERAL
%token <num> NUMBER

%%

program:
    /* empty */
    | program include_stmt
    | program class_decl
    | program function_decl
    ;

include_stmt:
    INCLUDE LBRACE include_list RBRACE
    | INCLUDE LBRACE include_list RBRACE SEMICOLON
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
    PUBLIC CLASS IDENTIFIER LBRACE class_body RBRACE
    | CLASS IDENTIFIER LBRACE class_body RBRACE
    ;

class_body:
    /* empty */
    | class_body member_decl
    ;

member_decl:
    field_decl
    | method_decl
    ;

field_decl:
    PRIVATE type IDENTIFIER SEMICOLON
    | PUBLIC type IDENTIFIER SEMICOLON
    | type IDENTIFIER SEMICOLON
    ;

method_decl:
    type IDENTIFIER LPAREN param_list RPAREN LBRACE stmt_list RBRACE
    | type QUESTION IDENTIFIER LPAREN param_list RPAREN LBRACE stmt_list RBRACE  /* Error? */
    ;

function_decl:
    type IDENTIFIER LPAREN param_list RPAREN LBRACE stmt_list RBRACE
    | type QUESTION IDENTIFIER LPAREN param_list RPAREN LBRACE stmt_list RBRACE
    ;

param_list:
    /* empty */
    | params
    ;

params:
    type IDENTIFIER
    | params COMMA type IDENTIFIER
    ;

type:
    INT
    | STRING
    | ERROR
    | IDENTIFIER  /* for custom types like Player */
    ;

stmt_list:
    /* empty */
    | stmt_list stmt
    ;

stmt:
    var_decl
    | assignment
    | if_stmt
    | while_stmt
    | for_stmt
    | switch_stmt
    | return_stmt
    | defer_stmt
    | expr SEMICOLON
    ;

var_decl:
    type IDENTIFIER ASSIGN expr SEMICOLON
    | type IDENTIFIER DECLARE_ASSIGN expr SEMICOLON
    | type IDENTIFIER SEMICOLON
    ;

assignment:
    IDENTIFIER ASSIGN expr SEMICOLON
    ;

if_stmt:
    IF LPAREN expr RPAREN LBRACE stmt_list RBRACE
    | IF LPAREN expr RPAREN LBRACE stmt_list RBRACE ELSE LBRACE stmt_list RBRACE
    ;

while_stmt:
    WHILE LPAREN expr RPAREN LBRACE stmt_list RBRACE
    ;

for_stmt:
    FOR LPAREN var_decl expr SEMICOLON assignment RPAREN LBRACE stmt_list RBRACE  /* C-style */
    | FOR LPAREN IDENTIFIER IN expr RPAREN LBRACE stmt_list RBRACE                /* range-based */
    ;

switch_stmt:
    SWITCH LPAREN expr RPAREN LBRACE case_list RBRACE
    ;

case_list:
    /* empty */
    | case_list case_clause
    ;

case_clause:
    CASE expr LBRACE stmt_list RBRACE
    | DEFAULT LBRACE stmt_list RBRACE
    ;

return_stmt:
    RETURN expr SEMICOLON
    | RETURN SEMICOLON
    ;

defer_stmt:
    DEFER expr SEMICOLON
    ;

expr:
    NUMBER
    | STRING_LITERAL
    | IDENTIFIER
    | NIL
    | NEW IDENTIFIER LPAREN arg_list RPAREN
    | IDENTIFIER LPAREN arg_list RPAREN          /* function call */
    | expr DOT IDENTIFIER                        /* member access */
    | expr DOT IDENTIFIER LPAREN arg_list RPAREN /* method call */
    | expr LE expr
    | expr EQ expr
    | expr NE expr
    | LPAREN expr RPAREN
    ;

arg_list:
    /* empty */
    | args
    ;

args:
    expr
    | args COMMA expr
    ;

%%

void yyerror(const char* s) {
    fprintf(stderr, "Error line %d: %s\n", yylineno, s);
}

int main() {
    return yyparse();
}
