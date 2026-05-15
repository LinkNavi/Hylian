#include "lsp_diag.h"
#include <string.h>
#include <stdio.h>

LspDiag lsp_diags[LSP_DIAG_MAX];
int     lsp_diag_count = 0;

void lsp_diag_push(int start_line, int start_col,
                   int end_line,   int end_col,
                   LspDiagSeverity severity,
                   const char *message)
{
    if (lsp_diag_count >= LSP_DIAG_MAX) return;
    LspDiag *d = &lsp_diags[lsp_diag_count++];
    d->start_line = start_line;
    d->start_col  = start_col;
    d->end_line   = end_line;
    d->end_col    = end_col;
    d->severity   = severity;
    snprintf(d->message, sizeof(d->message), "%s", message);
}