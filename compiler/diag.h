#ifndef DIAG_H
#define DIAG_H

/*
 * One diagnostic path for the whole frontend.
 *
 * The parser and the typechecker used to write straight to stderr with ANSI
 * colour codes baked in. That worked for the CLI and made the frontend
 * unusable for anything else — which is why the LSP grew a SECOND parser,
 * lexer, AST and typechecker whose only real difference was that they pushed
 * diagnostics into a buffer instead of printing them. Those copies then
 * drifted: the LSP grammar lost `else if`, its typechecker didn't know about
 * stdlib constants or class methods, and both reported errors for code that
 * compiled and ran fine.
 *
 * With output behind a sink, there is one frontend. The CLI installs the
 * default sink (colour, stderr); the LSP installs one that records positions
 * and messages for publishDiagnostics.
 */

typedef enum {
    DIAG_ERROR = 1,
    DIAG_WARNING = 2,
    DIAG_NOTE = 3,
} DiagSeverity;

/* line is 1-based; 0 means "position unknown". col is 0-based; -1 = unknown.
   `hint` is an optional second line of advice, or NULL. */
typedef void (*DiagSink)(DiagSeverity sev, const char *file, int line, int col,
                         const char *message, const char *hint, void *user);

/* Install a sink. Passing NULL restores the default stderr printer. */
void diag_set_sink(DiagSink sink, void *user);

/* Emit one diagnostic. `fmt` is printf-style. */
void diag_emit(DiagSeverity sev, const char *file, int line, int col,
               const char *hint, const char *fmt, ...);

/* Counts since the last diag_reset_counts(). The CLI uses the error count to
   decide whether to keep going; the LSP ignores them. */
int  diag_error_count(void);
int  diag_warning_count(void);
void diag_reset_counts(void);

#endif /* DIAG_H */
