%{
#include "parser.tab.h"
#include <string.h>
#include <stdlib.h>

extern const char *current_parse_file;

/* Check if a raw string body (between outer quotes) contains {{ }} interpolation */
static int has_interpolation(const char *s) {
    for (int i = 0; s[i]; i++)
        if (s[i] == '{' && s[i+1] == '{') return 1;
    return 0;
}


%}

%option noyywrap
%option yylineno

%%

[ \t\r\n]+          { /* skip whitespace */ }
"//".*              { /* skip single-line comments */ }

"asm{"              {
    int capacity = 256;
    int len = 0;
    char *buf = malloc(capacity);
    int c;
    while ((c = input()) != EOF) {
        if (c == '}') break;
        if (len + 2 >= capacity) {
            capacity *= 2;
            buf = realloc(buf, capacity);
        }
        buf[len++] = (char)c;
        if (c == '\n') yylineno++;
    }
    buf[len] = '\0';
    yylval.str = buf;
    return ASM_BLOCK;
}
"include"           { return INCLUDE; }
"class"             { return CLASS; }
"public"            { return PUBLIC; }
"private"           { return PRIVATE; }
"if"                { return IF; }
"else"              { return ELSE; }
"return"            { return RETURN; }
"new"               { return NEW; }
"nil"               { return NIL; }
"true"              { return TRUE_LIT; }
"false"             { return FALSE_LIT; }
"while"             { return WHILE; }
"for"               { return FOR; }
"in"                { return IN; }
"break"             { return BREAK; }
"continue"          { return CONTINUE; }
"ccpinclude"        { return CCPINCLUDE; }
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
"bool"              { return BOOL; }

"int"               { return INT; }
"str"               { return STRING; }
"Error"             { return ERROR; }

"array"   { return ARRAY; }
"multi"   { return MULTI; }
"any"     { return ANY_KW; }
[a-zA-Z_][a-zA-Z0-9_]*  { yylval.str = strdup(yytext); return IDENTIFIER; }
[0-9]+\.[0-9]+           { yylval.str = strdup(yytext); return FLOAT_LITERAL; }
[0-9]+                   { yylval.num = atoi(yytext); return NUMBER; }
\"([^\\\"]|\\.)*\"       {
    /* yytext includes the surrounding quotes */
    if (has_interpolation(yytext + 1)) {
        yylval.str = strdup(yytext);
        return INTERP_STRING;
    }
    yylval.str = strdup(yytext);
    return STRING_LITERAL;
}

"{"                 { return LBRACE; }
"}"                 { return RBRACE; }
"("                 { return LPAREN; }
")"                 { return RPAREN; }
"["                 { return LBRACKET; }
"]"                 { return RBRACKET; }
";"                 { return SEMICOLON; }
","                 { return COMMA; }
"."                 { return DOT; }
"?"                 { return QUESTION; }
"++"                { return INC; }
"--"                { return DEC; }
"+="                { return PLUS_ASSIGN; }
"-="                { return MINUS_ASSIGN; }
"*="                { return STAR_ASSIGN; }
"/="                { return SLASH_ASSIGN; }
"%="                { return MOD_ASSIGN; }
"="                 { return ASSIGN; }
":="                { return DECLARE_ASSIGN; }
">="                { return GE; }
"<="                { return LE; }
"=="                { return EQ; }
"!="                { return NE; }
"&&"                { return AND; }
"||"                { return OR; }
"!"                 { return NOT; }
">"                 { return GT; }
"<"                 { return LT; }
"+"                 { return PLUS; }
"-"                 { return MINUS; }
"*"                 { return STAR; }
"/"                 { return SLASH; }
"%"                 { return MOD; }

"|"                 { return PIPE; }
"&"                 { return AMP; }
"$"                 { /* swallow stray $ (legacy prefix, no longer needed) */ }
.                   { fprintf(stderr, "\033[1m%s:%d:\033[0m \033[1;31merror:\033[0m unexpected character '%s'\n", current_parse_file, yylineno, yytext); }

%%
