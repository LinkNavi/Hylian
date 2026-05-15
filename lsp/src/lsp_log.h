#ifndef LSP_LOG_H
#define LSP_LOG_H

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define LSP_LOG_MAX_BYTES (10 * 1024 * 1024) // 10 MB

static inline const char *lsp_log_file(void) {
    static char path[512] = {0};
    if (path[0]) return path;

    const char *home = getenv("HOME");
    if (!home) home = "/tmp";

    snprintf(path, sizeof(path), "%s/.local/share/hylian", home);

    // mkdir -p the directory
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);

    snprintf(path, sizeof(path), "%s/.local/share/hylian/lsp.log", home);
    return path;
}

static inline void lsp_log(const char *fmt, ...) {
    const char *log_file = lsp_log_file();

    // Rotate if over size limit
    struct stat st;
    if (stat(log_file, &st) == 0 && st.st_size >= LSP_LOG_MAX_BYTES) {
        char old[520];
        snprintf(old, sizeof(old), "%s.old", log_file);
        rename(log_file, old);
    }

    FILE *f = fopen(log_file, "a");
    if (!f) return;
    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);
    fprintf(f, "\n");
    fclose(f);
}

#endif // LSP_LOG_H
