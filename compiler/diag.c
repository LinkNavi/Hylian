#include "diag.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static DiagSink g_sink = NULL;
static void    *g_user = NULL;
static int      g_errors = 0;
static int      g_warnings = 0;

static int g_span = 0;
void diag_set_span(int len) { g_span = len; }
int  diag_span_len(void)    { return g_span; }

void diag_strip_ansi(char *s) {
    char *w = s;
    for (char *r = s; *r; r++) {
        if (*r == '\033' && r[1] == '[') {
            r += 2;
            while (*r && *r != 'm') r++;
            if (!*r) break;
            continue;
        }
        *w++ = *r;
    }
    *w = '\0';
}

static int is_word_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

int diag_guess_span(const char *line_text, const char *message, int col,
                    int span_len, int *out_col, int *out_len) {
    int n = (int)strlen(line_text);
    while (n > 0 && (line_text[n - 1] == '\n' || line_text[n - 1] == '\r')) n--;
    if (col >= 0 && col <= n && span_len > 0) {
        /* a wide span (a whole call or member expression): narrow it to the
           'quoted' name from the message when that name sits inside, since
           that word - not the whole expression - is what is wrong */
        const char *q1 = strchr(message, '\'');
        const char *q2 = q1 ? strchr(q1 + 1, '\'') : NULL;
        if (q1 && q2 && q2 > q1 + 1 && q2 - q1 - 1 < 128) {
            int nl = (int)(q2 - q1 - 1);
            int end = col + span_len < n ? col + span_len : n;
            int found = -1;
            for (int i = col; i + nl <= end; i++) {
                if (strncmp(line_text + i, q1 + 1, (size_t)nl) != 0) continue;
                if (is_word_char(q1[1]) && ((i > 0 && is_word_char(line_text[i - 1])) ||
                                            (i + nl < n && is_word_char(line_text[i + nl])))) continue;
                found = i;
                if (i > 0 && line_text[i - 1] == '.') break; /* prefer the member */
            }
            if (found >= 0) { *out_col = found; *out_len = nl; return 1; }
        }
    }
    if (col >= 0 && col <= n) {
        int len = span_len > 0 ? span_len : 1;
        if (span_len <= 0 && col < n && is_word_char(line_text[col]))
            for (len = 0; col + len < n && is_word_char(line_text[col + len]); len++) {}
        if (col + len > n && col < n) len = n - col;
        *out_col = col; *out_len = len;
        return 1;
    }
    /* first 'quoted' name in the message */
    const char *q1 = strchr(message, '\'');
    if (q1) {
        const char *q2 = strchr(q1 + 1, '\'');
        if (q2 && q2 > q1 + 1 && q2 - q1 - 1 < 128) {
            char name[128];
            memcpy(name, q1 + 1, (size_t)(q2 - q1 - 1));
            name[q2 - q1 - 1] = '\0';
            int nl = (int)strlen(name);
            for (int i = 0; i + nl <= n; i++) {
                if (strncmp(line_text + i, name, (size_t)nl) != 0) continue;
                int word = is_word_char(name[0]);
                if (word && i > 0 && is_word_char(line_text[i - 1])) continue;
                if (word && i + nl < n && is_word_char(line_text[i + nl])) continue;
                *out_col = i; *out_len = nl;
                return 1;
            }
        }
    }
    int s = 0;
    while (s < n && (line_text[s] == ' ' || line_text[s] == '\t')) s++;
    *out_col = s; *out_len = n > s ? n - s : 1;
    return 0;
}

/* Read one 1-based line of a file into buf. */
static int read_source_line(const char *file, int line, char *buf, size_t bufsz) {
    if (!file || line <= 0) return 0;
    FILE *f = fopen(file, "r");
    if (!f) return 0;
    int ok = 0;
    for (int i = 1; fgets(buf, (int)bufsz, f); i++)
        if (i == line) { ok = 1; break; }
    fclose(f);
    return ok;
}

/* The CLI format: `file:line:col: error: message`, then the offending source
   line with a caret underline, then an indented hint - the layout clang
   uses, so editors' "problem matchers" pick the location up too. */
static void default_sink(DiagSeverity sev, const char *file, int line, int col,
                         const char *message, const char *hint, void *user) {
    (void)user;
    const char *label =
        sev == DIAG_ERROR   ? "\033[1;31merror:\033[0m"   :
        sev == DIAG_WARNING ? "\033[1;33mwarning:\033[0m" :
                              "\033[1;36mnote:\033[0m";

    char text[1024];
    int have_src = read_source_line(file, line, text, sizeof(text));
    int gc = -1, gl = 0;
    if (have_src) diag_guess_span(text, message, col, g_span, &gc, &gl);

    if (gc >= 0) fprintf(stderr, "\033[1m%s:%d:%d:\033[0m %s %s\n",
                         file ? file : "<input>", line, gc + 1, label, message);
    else         fprintf(stderr, "\033[1m%s:%d:\033[0m %s %s\n",
                         file ? file : "<input>", line, label, message);

    if (have_src && gc >= 0) {
        int n = (int)strlen(text);
        while (n > 0 && (text[n - 1] == '\n' || text[n - 1] == '\r')) text[--n] = '\0';
        fprintf(stderr, " %5d | %s\n       | ", line, text);
        for (int i = 0; i < gc; i++) fputc(text[i] == '\t' ? '\t' : ' ', stderr);
        fprintf(stderr, "\033[1;32m^");
        for (int i = 1; i < gl; i++) fputc('~', stderr);
        fprintf(stderr, "\033[0m\n");
    }

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
    g_span = 0;
}

int  diag_error_count(void)   { return g_errors; }
int  diag_warning_count(void) { return g_warnings; }
void diag_reset_counts(void)  { g_errors = 0; g_warnings = 0; }
