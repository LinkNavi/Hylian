#ifndef TYPECHECK_H
#define TYPECHECK_H
#include "ast.h"

/* Run the type inference pass over a fully-merged ProgramNode in LSP mode.
   Instead of writing to stderr, all diagnostics are pushed into the
   lsp_diag buffer (see lsp_diag.h) so the LSP server can relay them to
   the editor as textDocument/publishDiagnostics notifications.
   filename is used in diagnostic messages (pass the source file path). */
void lsp_typecheck(ProgramNode *program, const char *filename);
/* External function signature for C stdlib integration */
typedef struct {
    const char *name;
    const char *return_type;  /* e.g. "str", "int", "void" */
    int param_count;
    /* We could add param types here later if needed */
} TCExternalFunc;

void lsp_typecheck_with_externals(ProgramNode *program, const char *filename,
                                  TCExternalFunc *ext, int ext_count);
#endif
