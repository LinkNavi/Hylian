#ifndef LSP_LOG_H
#define LSP_LOG_H

#include <stdio.h>
#include <stdarg.h>

// Path to the log file
#define LSP_LOG_FILE "/tmp/hylian_lsp.log"

static inline void lsp_log(const char *fmt, ...) {
    FILE *f = fopen(LSP_LOG_FILE, "a");
    if (!f) return;
    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);
    fprintf(f, "\n");
    fclose(f);
}

#endif // LSP_LOG_H
