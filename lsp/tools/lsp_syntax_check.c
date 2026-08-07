/*
 * lsp_syntax_check — run one .hy file through the LSP's own grammar and report
 * whether it parsed, without needing an editor or an LSP client in the loop.
 *
 * This exists so the LSP grammar is testable at all. It is a separate bison
 * grammar from compiler/parser.y, and the two silently drifted apart: the LSP
 * copy had no `else if` rule, so every `else if` in a real project showed up in
 * the editor as a syntax error while the same file compiled and ran fine. There
 * was no way to notice that short of opening a file and looking at it.
 *
 * Exit status: 0 = parsed cleanly, 1 = syntax errors (printed to stdout),
 * 2 = usage/IO problem. tests/grammar_drift.sh compares this against the real
 * compiler's verdict on the same corpus.
 */
#include <stdio.h>
#include <stdlib.h>
#include "ast.h"
#include "../src/lsp_diag.h"
#include "diag.h"

extern int  yyparse(void);
extern void yyrestart(FILE *f);
extern int  yylineno;
extern const char *current_parse_file;
extern ProgramNode *root;

/* Capture the shared frontend's diagnostics instead of letting them print. */
static void capture_sink(DiagSeverity sev, const char *file, int line, int col,
                         const char *message, const char *hint, void *user) {
    (void)file; (void)col; (void)hint; (void)user;
    lsp_diag_push(line > 0 ? line - 1 : 0, 0, line > 0 ? line - 1 : 0, 999,
                  sev == DIAG_WARNING ? LSP_DIAG_WARNING : LSP_DIAG_ERROR,
                  message);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: lsp_syntax_check <file.hy>\n");
        return 2;
    }

    FILE *f = fopen(argv[1], "r");
    if (!f) {
        fprintf(stderr, "lsp_syntax_check: cannot open '%s'\n", argv[1]);
        return 2;
    }

    lsp_diag_clear();
    diag_set_sink(capture_sink, NULL);
    root = NULL;
    current_parse_file = argv[1];
    yyrestart(f);
    yylineno = 1;
    yyparse();
    fclose(f);
    diag_set_sink(NULL, NULL);

    /* The LSP parser reports through the diagnostic buffer rather than by
       return code, so that's what decides pass/fail here. */
    int errors = 0;
    for (int i = 0; i < lsp_diag_count; i++) {
        if (lsp_diags[i].severity != LSP_DIAG_ERROR) continue;
        errors++;
        printf("%s:%d:%d: %s\n", argv[1],
               lsp_diags[i].start_line + 1,
               lsp_diags[i].start_col + 1,
               lsp_diags[i].message);
    }

    if (!root && errors == 0) {
        printf("%s: parser produced no AST and reported no error\n", argv[1]);
        errors++;
    }

    return errors ? 1 : 0;
}
