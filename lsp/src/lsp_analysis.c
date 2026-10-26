/* lsp_analysis.c - analyse one file: parse, resolve includes, typecheck, and
 * turn what the frontend reports into editor diagnostics with exact ranges. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lsp_internal.h"
#include "typecheck.h"
#include "diag.h"
#include "lsp_log.h"

extern ProgramNode *lsp_parse_text(const char *path, const char *text);
extern void lsp_capture_begin(void);
extern void lsp_capture_end(void);
extern int  lsp_capture_count(void);
extern int  lsp_capture_get(int i, int *sev, int *line, int *col, int *span,
                            const char **message, const char **hint);

/* ── project ─────────────────────────────────────────────────────────────── */

LspProject *lsp_project_create(const char *workspace_root) {
    LspProject *p = calloc(1, sizeof(LspProject));
    snprintf(p->workspace, sizeof(p->workspace), "%s", workspace_root ? workspace_root : ".");
    return p;
}

void lsp_project_free(LspProject *p) {
    if (!p) return;
    for (int i = 0; i < p->mod_count; i++) { free(p->mods[i]->text); free(p->mods[i]); }
    free(p->mods);
    for (int i = 0; i < p->overlay_count; i++) free(p->overlays[i].text);
    free(p);
}

/* ── diagnostics ─────────────────────────────────────────────────────────── */

static void push_diag(LspState *st, int sev, int line0, int col, int len, const char *msg, const char *hint) {
    if (st->diag_count >= LSP_DIAG_MAX) return;
    LspDiag *d = &st->diags[st->diag_count++];
    d->start_line = d->end_line = line0;
    d->start_col = col;
    d->end_col = col + (len > 0 ? len : 1);
    d->severity = sev == DIAG_WARNING ? LSP_DIAG_WARNING : sev == DIAG_NOTE ? LSP_DIAG_INFO : LSP_DIAG_ERROR;
    if (hint && hint[0]) snprintf(d->message, sizeof(d->message), "%s\n%s", msg, hint);
    else                 snprintf(d->message, sizeof(d->message), "%s", msg);
}

/* Convert everything the frontend just reported into positioned diagnostics.
   `drop_undefined`: an include failed to resolve, so "undefined X" errors are
   just fallout from that and would bury the real cause. */
static void drain_captured(LspState *st, int drop_undefined) {
    int n = lsp_capture_count();
    for (int i = 0; i < n; i++) {
        int sev, line, col, span;
        const char *msg, *hint;
        if (!lsp_capture_get(i, &sev, &line, &col, &span, &msg, &hint)) continue;
        if (drop_undefined && sev == DIAG_ERROR &&
            (!strncmp(msg, "undefined variable", 18) || !strncmp(msg, "call to undefined function", 26)))
            continue;
        int line0 = line > 0 ? line - 1 : 0;
        int ls, le;
        int c = 0, l = 1;
        if (lq_line_bounds(st->text, line0, &ls, &le)) {
            char buf[1024];
            int len = le - ls < (int)sizeof(buf) - 1 ? le - ls : (int)sizeof(buf) - 1;
            memcpy(buf, st->text + ls, (size_t)len);
            buf[len] = '\0';
            diag_guess_span(buf, msg, col, span, &c, &l);
        }
        push_diag(st, sev, line0, c, l, msg, hint);
    }
}

/* Where an include path is written, so an unresolved one can be underlined. */
static int find_include_token(const char *text, const char *inc, int *line, int *col) {
    size_t n = strlen(inc);
    for (const char *p = text; (p = strstr(p, inc)) != NULL; p += n) {
        char before = p > text ? p[-1] : ' ';
        char after = p[n];
        if (lq_is_ident(before) || before == '.') continue;
        if (lq_is_ident(after) || after == '.') continue;
        lq_position(text, (int)(p - text), line, col);
        return 1;
    }
    return 0;
}

/* ── analysis ────────────────────────────────────────────────────────────── */

LspState *lsp_analyze(LspProject *proj, const char *path, const char *text) {
    LspState *st = calloc(1, sizeof(LspState));
    st->proj = proj;
    snprintf(st->path, sizeof(st->path), "%s", path);
    st->text = strdup(text ? text : "");
    zora_info_for_file(path, &st->zora);

    /* a file inside the stdlib tree resolves its siblings against the stdlib root */
    if (!st->zora.found) {
        char root[1024];
        std_root_for(&st->zora, root, sizeof(root));
        size_t rl = strlen(root);
        if (rl && !strncmp(path, root, rl) && path[rl] == '/') {
            snprintf(st->zora.src_dir, sizeof(st->zora.src_dir), "%s", root);
            snprintf(st->zora.root, sizeof(st->zora.root), "%s", root);
        }
    }

    /* the buffer being edited shadows disk for anything that includes it */
    lsp_project_set_overlay(proj, path, st->text);

    lsp_capture_begin();
    st->prog = lsp_parse_text(path, st->text);
    int syntax_errors = 0;
    for (int i = 0; i < lsp_capture_count(); i++) {
        int sev, line, col, span; const char *m, *h;
        if (lsp_capture_get(i, &sev, &line, &col, &span, &m, &h) && sev == DIAG_ERROR) syntax_errors++;
    }
    drain_captured(st, 0);
    lsp_capture_end();

    if (!st->prog) return st;

    deps_collect(st);

    for (int i = 0; i < st->unresolved_count; i++) {
        int line, col;
        if (find_include_token(st->text, st->unresolved[i].inc, &line, &col))
            push_diag(st, DIAG_ERROR, line, col, (int)strlen(st->unresolved[i].inc),
                      st->unresolved[i].reason, NULL);
        else
            push_diag(st, DIAG_ERROR, 0, 0, 1, st->unresolved[i].reason, NULL);
    }

    /* Typecheck always: it annotates every expression with its type, which
       completion and hover rely on. With a syntax error in the file its
       complaints are mostly fallout from that error, so they are dropped and
       only the parser's are shown. */
    {
        ProgramNode *check = build_check_program(st);
        lsp_capture_begin();
        typecheck(check, path);
        if (syntax_errors == 0) drain_captured(st, st->unresolved_count > 0);
        lsp_capture_end();
        free(check->declarations);
        free(check);
    }
    return st;
}

void lsp_state_free(LspState *st) {
    if (!st) return;
    free(st->text);
    free(st);
}

const LspDiag *lsp_state_diags(const LspState *st, int *count) {
    *count = st->diag_count;
    return st->diags;
}

const ZoraInfo *lsp_state_zora(const LspState *st) { return &st->zora; }
