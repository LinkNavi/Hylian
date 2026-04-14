%{
#include "parser.tab.h"
%}

%option noyywrap
%option yylineno

%%

[ \t\r\n]+          { /* skip whitespace */ }
"//".*              { /* skip single-line comments */ }

"include"           { return INCLUDE; }
"class"             { return CLASS; }
"public"            { return PUBLIC; }
"private"           { return PRIVATE; }
"if"                { return IF; }
"else"              { return ELSE; }
"return"            { return RETURN; }
"new"               { return NEW; }
"nil"               { return NIL; }
"while"             { return WHILE; }
"for"               { return FOR; }
"in"                { return IN; }
"break"             { return BREAK; }
"ccpinclude"	    { return CCPINCLUDE; }
"continue"          { return CONTINUE; }
"switch"            { return SWITCH; }
"case"              { return CASE; }
"default"           { return DEFAULT; }
"defer"             { return DEFER; }
"match"             { return MATCH; }
"unsafe"            { return UNSAFE; }
"const"             { return CONST; }
"static"            { return STATIC; }
"extern"            { return EXTERN; }
"void"              { return VOID; }

"int"               { return INT; }
"str"            { return STRING; }
"Error"             { return ERROR; }

[a-zA-Z_][a-zA-Z0-9_]*  { yylval.str = strdup(yytext); return IDENTIFIER; }
[0-9]+                  { yylval.num = atoi(yytext); return NUMBER; }
\"([^\\\"]|\\.)*\"      { yylval.str = strdup(yytext); return STRING_LITERAL; }

"{"                 { return LBRACE; }
"}"                 { return RBRACE; }
"("                 { return LPAREN; }
")"                 { return RPAREN; }
";"                 { return SEMICOLON; }
","                 { return COMMA; }
"."                 { return DOT; }
"?"                 { return QUESTION; }
"="                 { return ASSIGN; }
":="                { return DECLARE_ASSIGN; }
">"                 { return GT; }
"<"                 { return LT; }
"+"                 { return PLUS; }
"-"                 { return MINUS; }
"<="                { return LE; }
"=="                { return EQ; }
"!="                { return NE; }

.                   { printf("Unknown char: %s\n", yytext); }

%%
