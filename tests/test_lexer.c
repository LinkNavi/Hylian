#include "parser.tab.h"
#include <stdio.h>

extern int yylex();
extern char* yytext;

YYSTYPE yylval;  // Add this

int main() {
    int token;
    while ((token = yylex())) {
        printf("Token %d: '%s'\n", token, yytext);
    }
    return 0;
}
