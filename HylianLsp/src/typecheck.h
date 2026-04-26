#ifndef TYPECHECK_H
#define TYPECHECK_H
#include "ast.h"

/* Run the type inference pass over a fully-merged ProgramNode in LSP mode.
   Instead of writing to stderr, all diagnostics are pushed into the
   lsp_diag buffer (see lsp_diag.h) so the LSP server can relay them to
   the editor as textDocument/publishDiagnostics notifications.
   filename is used in diagnostic messages (pass the source file path). */
void lsp_typecheck(ProgramNode *program, const char *filename);

#endif