#ifndef LSP_DIAG_H
#define LSP_DIAG_H

/* ─── LSP Diagnostic buffer ─────────────────────────────────────────────────
   The parser, lexer, and typecheck pass all call lsp_diag_push() instead of
   printing to stderr.  The main LSP analysis layer drains this buffer and
   converts it to a textDocument/publishDiagnostics JSON payload.
   ─────────────────────────────────────────────────────────────────────────── */

#define LSP_DIAG_MAX 256

typedef enum {
    LSP_DIAG_ERROR   = 1,
    LSP_DIAG_WARNING = 2,
    LSP_DIAG_INFO    = 3,
    LSP_DIAG_HINT    = 4,
} LspDiagSeverity;

typedef struct {
    int start_line;   /* 0-based */
    int start_col;    /* 0-based */
    int end_line;     /* 0-based */
    int end_col;      /* 0-based */
    LspDiagSeverity severity;
    char message[512];
} LspDiag;

/* Global buffer — reset with lsp_diag_clear() before each analysis pass */
extern LspDiag   lsp_diags[LSP_DIAG_MAX];
extern int       lsp_diag_count;

/* Clear all collected diagnostics */
static inline void lsp_diag_clear(void) {
    lsp_diag_count = 0;
}

/* Push one diagnostic (silently drops if buffer is full) */
void lsp_diag_push(int start_line, int start_col,
                   int end_line,   int end_col,
                   LspDiagSeverity severity,
                   const char *message);

#endif /* LSP_DIAG_H */