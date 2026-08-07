#include "diag.h"
#include <stdarg.h>
#include <stdio.h>

static DiagSink g_sink = NULL;
static void    *g_user = NULL;
static int      g_errors = 0;
static int      g_warnings = 0;

/* The original CLI format, preserved exactly: bold file:line, coloured
   severity, and an indented cyan-highlighted hint underneath. */
static void default_sink(DiagSeverity sev, const char *file, int line, int col,
                         const char *message, const char *hint, void *user) {
    (void)col; (void)user;

    const char *label =
        sev == DIAG_ERROR   ? "\033[1;31merror:\033[0m"   :
        sev == DIAG_WARNING ? "\033[1;33mwarning:\033[0m" :
                              "\033[1;36mnote:\033[0m";

    fprintf(stderr, "\033[1m%s:%d:\033[0m %s %s\n",
            file ? file : "<input>", line, label, message);

    if (hint && hint[0])
        fprintf(stderr, "  \033[1;33mhint:\033[0m %s\n", hint);
}

void diag_set_sink(DiagSink sink, void *user) {
    g_sink = sink;
    g_user = user;
}

void diag_emit(DiagSeverity sev, const char *file, int line, int col,
               const char *hint, const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (sev == DIAG_ERROR)        g_errors++;
    else if (sev == DIAG_WARNING) g_warnings++;

    if (g_sink) g_sink(sev, file, line, col, buf, hint, g_user);
    else        default_sink(sev, file, line, col, buf, hint, NULL);
}

int  diag_error_count(void)   { return g_errors; }
int  diag_warning_count(void) { return g_warnings; }
void diag_reset_counts(void)  { g_errors = 0; g_warnings = 0; }
