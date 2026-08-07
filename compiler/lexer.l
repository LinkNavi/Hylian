%{
#include "parser.tab.h"
#include <string.h>
#include <stdlib.h>

extern const char *current_parse_file;
static int input(void);
extern const char *current_compile_target;

/* Skip tokens for a top-level declaration (function or class body).
   Called after @target(X) when X doesn't match the current target.
   Skips: a single-line decl (ends at ';' with brace_depth==0),
   or a braced decl (ends when brace_depth returns to 0 after first '{').*/
static void skip_target_decl(void) {
    int c;
    int brace_depth = 0;
    int seen_brace = 0;
    while ((c = input()) != EOF) {
        if (c == '\n') yylineno++;
        if (c == '{') { brace_depth++; seen_brace = 1; }
        else if (c == '}') {
            brace_depth--;
            if (seen_brace && brace_depth == 0) return;
        } else if (c == ';' && !seen_brace) {
            return;
        }
    }
}

/* Check if a raw string body (between outer quotes) contains {{ }} interpolation */
static int has_interpolation(const char *s) {
    for (int i = 0; s[i]; i++)
        if (s[i] == '{' && s[i+1] == '{') return 1;
    return 0;
}

/* Decode backslash escapes in a quoted string literal, in place of the raw
   source text. `src` still includes the surrounding double quotes and the
   result keeps them, because every downstream consumer (lower.c, the interp
   splitter, the .hyi reader) expects a quoted literal and strips the quotes
   itself.
   Before this existed the escapes were simply never processed anywhere in the
   pipeline - "\n" was the two characters '\' and 'n', which is why the
   runtime had to write its newline from a byte-valued local instead of a
   string constant.
   A decoded '"' is emitted as a bare quote inside the body. That's safe
   because the quote-stripping downstream only looks at the first and last
   character, never rescans for a closing quote. An unrecognized escape keeps
   its backslash and the following character verbatim, so a typo stays visible
   instead of silently vanishing. */
static char *decode_escapes(const char *src) {
    size_t len = strlen(src);
    char *out = malloc(len + 1); /* decoding never grows the string */
    size_t o = 0;

    for (size_t i = 0; i < len; i++) {
        if (src[i] != '\\' || i + 1 >= len) { out[o++] = src[i]; continue; }
        char c = src[++i];
        switch (c) {
        case 'n':  out[o++] = '\n'; break;
        case 't':  out[o++] = '\t'; break;
        case 'r':  out[o++] = '\r'; break;
        case '0':  out[o++] = '\0'; break;
        case 'a':  out[o++] = '\a'; break;
        case 'b':  out[o++] = '\b'; break;
        case 'f':  out[o++] = '\f'; break;
        case 'v':  out[o++] = '\v'; break;
        case 'e':  out[o++] = 27;   break; /* ESC, for ANSI colour codes */
        case '\\': out[o++] = '\\'; break;
        case '\'': out[o++] = '\''; break;
        case '"':  out[o++] = '"';  break;
        case 'x': {
            /* \xNN - exactly one or two hex digits */
            int val = 0, digits = 0;
            while (digits < 2 && i + 1 < len) {
                char h = src[i + 1];
                int d;
                if      (h >= '0' && h <= '9') d = h - '0';
                else if (h >= 'a' && h <= 'f') d = h - 'a' + 10;
                else if (h >= 'A' && h <= 'F') d = h - 'A' + 10;
                else break;
                val = val * 16 + d;
                i++;
                digits++;
            }
            if (digits == 0) { out[o++] = '\\'; out[o++] = 'x'; }
            else             { out[o++] = (char)val; }
            break;
        }
        default:
            out[o++] = '\\';
            out[o++] = c;
            break;
        }
    }
    out[o] = '\0';
    return out;
}

/* Strip underscore digit-separators from a numeric literal string */
static char *strip_underscores(const char *s) {
    char *buf = malloc(strlen(s) + 1);
    int j = 0;
    for (int i = 0; s[i]; i++)
        if (s[i] != '_') buf[j++] = s[i];
    buf[j] = '\0';
    return buf;
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
"asm {"              {
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
"module"            { return MODULE; }
"class"             { return CLASS; }
"enum"              { return ENUM; }
"public"            { return PUBLIC; }
"private"           { return PRIVATE; }
"if"                { return IF; }
"else"              { return ELSE; }
"return"            { return RETURN; }
"new"               { return NEW; }
"nil"               { return NIL; }
"null"              { return NIL; }
"true"              { return TRUE_LIT; }
"false"             { return FALSE_LIT; }
"addrof_fn"  { return ADDROF_FN; }
"adrof_fn"   { return ADDROF_FN; }
"while"             { return WHILE; }

"for"               { return FOR; }
"in"                { return IN; }
"break"             { return BREAK; }
"continue"          { return CONTINUE; }
"ccpinclude"        { return CCPINCLUDE; }
"switch"            { return SWITCH; }
"case"              { return CASE; }
"default"           { return DEFAULT; }
"unsafe"            { return UNSAFE; }
"const"             { return CONST; }
"static"            { return STATIC; }
"void"              { return VOID; }
"bool"              { return BOOL; }
"volatile"          { return VOLATILE; }
"packed"            { return PACKED; }
"union"             { return UNION_KW; }
"struct"            { return STRUCT; }
"naked"             { return NAKED; }
"usize"             { return USIZE; }
"isize"             { return ISIZE; }

"int"               { return INT; }
"str"               { return STRING; }
"Error"             { return ERROR; }

"array"   { return ARRAY; }
"multi"   { return MULTI; }
"any"     { return ANY_KW; }
"cast"    { return CAST; }
"as"      { return AS; }
"size_of" { return SIZE_OF; }
[a-zA-Z_][a-zA-Z0-9_]*  { yylval.str = strdup(yytext); return IDENTIFIER; }
[0-9]+\.[0-9]+           { yylval.str = strdup(yytext); return FLOAT_LITERAL; }
0[xX][0-9a-fA-F_]+  {
    char *clean = strip_underscores(yytext);
    yylval.num = (long)(unsigned long long)strtoull(clean, NULL, 16);
    free(clean);
    return NUMBER;
}
[0-9][0-9_]*  {
    char *clean = strip_underscores(yytext);
    yylval.num = (long)(unsigned long long)strtoull(clean, NULL, 10);
    free(clean);
    return NUMBER;
}
\"([^\\\"]|\\.)*\"       {
    /* yytext includes the surrounding quotes; decode_escapes keeps them */
    if (has_interpolation(yytext + 1)) {
        yylval.str = decode_escapes(yytext);
        return INTERP_STRING;
    }
    yylval.str = decode_escapes(yytext);
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
":"                 { return COLON; }
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
"~"                 { return TILDE; }
"<<"                { return LSHIFT; }
">>"                { return RSHIFT; }
"^"                 { return XOR; }
"$"                 { /* swallow stray $ (legacy prefix, no longer needed) */ }
@target[ \t]*\([ \t]*[a-zA-Z_][a-zA-Z0-9_]*[ \t]*\) {
    /* Extract the target name from @target(...) */
    char *p = yytext;
    while (*p && *p != '(') p++;
    p++; /* skip '(' */
    while (*p == ' ' || *p == '\t') p++;
    char tname[64]; int tlen = 0;
    while (*p && *p != ')' && *p != ' ' && *p != '\t' && tlen < 63)
        tname[tlen++] = *p++;
    tname[tlen] = '\0';
    if (strcmp(tname, current_compile_target) != 0)
        skip_target_decl();
    /* else: target matches, parse the following declaration normally */
}
.                   { fprintf(stderr, "\033[1m%s:%d:\033[0m \033[1;31merror:\033[0m unexpected character '%s'\n", current_parse_file, yylineno, yytext); }

%%
