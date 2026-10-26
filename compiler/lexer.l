%{
#include "parser.tab.h"
#include <string.h>
#include <stdlib.h>
#include "diag.h"

extern const char *current_parse_file;
static int input(void);

/* Column tracking for diagnostics: lex_tok_col/lex_tok_len describe the token
   most recently returned (0-based column, length in bytes), which is the
   lookahead token when the parser reports a syntax error. Reset lex_col to 0
   before parsing a new buffer. */
int lex_col = 0, lex_tok_col = 0, lex_tok_len = 0, lex_tok_line = 1, lex_tok_first = 0;
/* the token BEFORE the current one: where it ended and what it ended with */
int lex_prev_line = 0, lex_prev_end_col = 0, lex_prev_last = 0;
static int cur_end_line = 0, cur_end_col = 0, cur_last = 0;
/* advance the column for one character consumed by hand with input() (flex's
   input() already advances yylineno itself under %option yylineno - bumping it
   here too used to double-count every line inside a block comment) */
#define LEX_ADV(c) do { if ((c) == '\n') lex_col = 0; else lex_col++; } while (0)
#define YY_USER_ACTION { \
    int _ws = (yytext[0] == ' ' || yytext[0] == '\t' || yytext[0] == '\n' || yytext[0] == '\r' || \
               (yytext[0] == '/' && (yytext[1] == '/' || yytext[1] == '*'))); \
    int _start = lex_col, _nl = 0; \
    for (int _i = 0; _i < yyleng; _i++) { if (yytext[_i] == '\n') { lex_col = 0; _nl++; } else lex_col++; } \
    if (!_ws) { \
        yylloc.first_line = yylineno - _nl; yylloc.first_column = _start; \
        yylloc.last_line = yylineno; yylloc.last_column = lex_col; \
        lex_prev_line = cur_end_line; lex_prev_end_col = cur_end_col; lex_prev_last = cur_last; \
        lex_tok_col = _start; lex_tok_len = (int)yyleng; lex_tok_first = yytext[0]; \
        lex_tok_line = yylineno; \
        cur_end_line = yylineno; cur_end_col = lex_col; cur_last = yytext[yyleng - 1]; \
    } }
extern const char *current_compile_target;

/* Skip tokens for a top-level declaration (function or class body).
   Called after @target(X) when X doesn't match the current target.
   Skips: a single-line decl (ends at ';' with brace_depth==0),
   or a braced decl (ends when brace_depth returns to 0 after first '{').*/
static void skip_target_decl(void) {
    int c;
    int brace_depth = 0;
    int seen_brace = 0;
    while ((c = input()) != EOF && c != 0) {
        LEX_ADV(c);
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


/* Append `s` (with len bytes) to a growing malloc'd buffer. */
static void sb_add(char **buf, size_t *len, size_t *cap, const char *s, size_t n) {
    if (*len + n + 1 > *cap) {
        while (*len + n + 1 > *cap) *cap = *cap ? *cap * 2 : 128;
        *buf = realloc(*buf, *cap);
    }
    memcpy(*buf + *len, s, n);
    *len += n;
    (*buf)[*len] = '\0';
}

/* Append a string-literal spelling of raw (still escaped, exactly as written
   in the source) literal text. It already contains valid escapes, so it is
   emitted verbatim inside quotes - except `{`, which is written as \x7b so an
   unmatched "{{" in a literal can never be mistaken for interpolation when
   the rewritten text is lexed again. */
static void sb_add_quoted(char **buf, size_t *len, size_t *cap, const char *text, size_t n) {
    sb_add(buf, len, cap, "\"", 1);
    for (size_t i = 0; i < n; i++) {
        if (text[i] == '{') sb_add(buf, len, cap, "\\x7b", 4);
        else sb_add(buf, len, cap, text + i, 1);
    }
    sb_add(buf, len, cap, "\"", 1);
}

/* Append expression source taken from inside a string literal: the only
   escapes that are undone are \" and \\, which the string form needed and the
   expression form doesn't ('\n' etc. are left for the char-literal rule). */
static void sb_add_expr(char **buf, size_t *len, size_t *cap, const char *text, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (text[i] == '\\' && i + 1 < n && (text[i + 1] == '"' || text[i + 1] == '\\')) i++;
        sb_add(buf, len, cap, text + i, 1);
    }
}

/* Rewrite an interpolated string body (raw source text between the quotes) `a {{x + 1}} b` into the token text
       __interp("a ", (x + 1), " b")
   which the parser reads as an ordinary call. lower.c gives __interp its
   meaning: convert each argument to text by its type and concatenate (so a
   lone {{expr}} still yields a string). The
   text is pushed back onto the input so the expressions are lexed and parsed
   by exactly the same code as everything else - no second expression parser. */
static char *rewrite_interpolation(const char *body, size_t blen) {
    char *out = NULL; size_t olen = 0, ocap = 0;
    sb_add(&out, &olen, &ocap, "__interp(", 9);
    size_t i = 0, lit_start = 0;
    int nargs = 0;
    while (i <= blen) {
        int at_open = (i + 1 < blen && body[i] == '{' && body[i + 1] == '{');
        if (at_open || i == blen) {
            size_t close = 0;
            if (at_open) {
                /* find the matching "}}"; if there is none the rest is literal */
                for (size_t j = i + 2; j + 1 < blen; j++)
                    if (body[j] == '}' && body[j + 1] == '}') { close = j; break; }
                if (!close) { i++; continue; }
            }
            if (i > lit_start || (i == blen && nargs == 0)) {
                if (nargs) sb_add(&out, &olen, &ocap, ", ", 2);
                sb_add_quoted(&out, &olen, &ocap, body + lit_start, i - lit_start);
                nargs++;
            }
            if (!at_open) break;
            if (nargs) sb_add(&out, &olen, &ocap, ", ", 2);
            sb_add(&out, &olen, &ocap, "(", 1);
            sb_add_expr(&out, &olen, &ocap, body + i + 2, close - (i + 2));
            sb_add(&out, &olen, &ocap, ")", 1);
            nargs++;
            i = close + 2;
            lit_start = i;
            continue;
        }
        i++;
    }
    sb_add(&out, &olen, &ocap, ")", 1);
    return out;
}

%}

%option noyywrap
%option yylineno

%%

[ \t\r\n]+          { /* skip whitespace */ }
"//".*              { /* skip single-line comments */ }
"/*"                {
    /* C-style block comment (not nested). flex's input() reports end of file
       as 0, not EOF - an unterminated comment used to loop here forever. */
    int c, prev = 0, closed = 0;
    int start_line = yylineno, start_col = lex_col - 2;
    while ((c = input()) != EOF && c != 0) {
        LEX_ADV(c);
        if (prev == '*' && c == '/') { closed = 1; break; }
        prev = c;
    }
    if (!closed) {
        diag_set_span(2);
        diag_emit(DIAG_ERROR, current_parse_file, start_line, start_col, NULL, "unterminated /* comment");
    }
}

"asm{"              {
    int capacity = 256;
    int len = 0;
    char *buf = malloc(capacity);
    int c;
    while ((c = input()) != EOF && c != 0) {
        if (c == '}') break;
        if (len + 2 >= capacity) {
            capacity *= 2;
            buf = realloc(buf, capacity);
        }
        buf[len++] = (char)c;
        LEX_ADV(c);
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
    while ((c = input()) != EOF && c != 0) {
        if (c == '}') break;
        if (len + 2 >= capacity) {
            capacity *= 2;
            buf = realloc(buf, capacity);
        }
        buf[len++] = (char)c;
        LEX_ADV(c);
    }
    buf[len] = '\0';
    yylval.str = buf;
    return ASM_BLOCK;
}
"include"           { return INCLUDE; }
"do"                { return DO; }
"defer"             { return DEFER; }
"typedef"           { return TYPEDEF; }
"sizeof"            { return SIZE_OF; }
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

"@section"          { return SECTION_ATTR; } /* placement attribute on a static, see parser.y */

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
[0-9][0-9_]*\.[0-9][0-9_]*([eE][+-]?[0-9]+)?  { yylval.str = strip_underscores(yytext); return FLOAT_LITERAL; }
[0-9][0-9_]*[eE][+-]?[0-9]+                  { yylval.str = strip_underscores(yytext); return FLOAT_LITERAL; }
0[bB][01_]+  {
    char *clean = strip_underscores(yytext + 2);
    yylval.num = (long)(unsigned long long)strtoull(clean, NULL, 2);
    free(clean);
    return NUMBER;
}
0[oO][0-7_]+  {
    char *clean = strip_underscores(yytext + 2);
    yylval.num = (long)(unsigned long long)strtoull(clean, NULL, 8);
    free(clean);
    return NUMBER;
}
'([^\\'\n]|\\x[0-9a-fA-F]{1,2}|\\.)'  {
    /* character literal: 'a', '\n', '\x41' - an int holding the byte value.
       Reuse the string-escape decoder on the text between the quotes. */
    char *inner = strdup(yytext + 1);
    inner[strlen(inner) - 1] = '\0';
    char *quoted = malloc(strlen(inner) + 3);
    sprintf(quoted, "\"%s\"", inner);
    char *dec = decode_escapes(quoted);
    yylval.num = (long)(unsigned char)dec[1];
    free(inner); free(quoted); free(dec);
    return NUMBER;
}
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
        char *rewritten = rewrite_interpolation(yytext + 1, strlen(yytext) - 2);
        for (int k = (int)strlen(rewritten) - 1; k >= 0; k--) unput(rewritten[k]);
        free(rewritten);
        /* no token of its own: the pushed-back __interp(...) call is lexed next */
    } else {
    yylval.str = decode_escapes(yytext);
    return STRING_LITERAL;
    }
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
"<<="               { return LSHIFT_ASSIGN; }
">>="               { return RSHIFT_ASSIGN; }
"&="                { return AND_ASSIGN; }
"|="                { return OR_ASSIGN; }
"^="                { return XOR_ASSIGN; }
"..="               { return DOTDOTEQ; }
".."                { return DOTDOT; }
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
