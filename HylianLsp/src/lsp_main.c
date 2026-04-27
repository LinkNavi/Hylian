#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lsp_proto.h"
#include "lsp_diag.h"
#include "lsp_log.h"
#include "lsp_analysis.h"

/* ─── Global project ─────────────────────────────────────────────────────────
   One LspProject lives for the whole session.  It is created once we receive
   the initialize request with a rootUri / rootPath.  If the client sends no
   root we still create a project with root_dir = "." so that at least the
   open file itself gets indexed.
   ─────────────────────────────────────────────────────────────────────────── */

static LspProject *g_project = NULL;

/* ─── Document store ─────────────────────────────────────────────────────────
   We keep one LspState per open document (identified by URI).
   ─────────────────────────────────────────────────────────────────────────── */

#define MAX_OPEN_DOCS 64

typedef struct {
    char      uri[1024];
    char     *text;       /* heap-allocated current content */
    LspState *state;      /* analysis result — rebuilt on every change */
} OpenDoc;

static OpenDoc open_docs[MAX_OPEN_DOCS];
static int     open_doc_count = 0;

static OpenDoc *doc_find(const char *uri) {
    for (int i = 0; i < open_doc_count; i++)
        if (strcmp(open_docs[i].uri, uri) == 0)
            return &open_docs[i];
    return NULL;
}

/* Strip "file://" prefix to get a real filesystem path */
static const char *uri_to_path(const char *uri) {
    if (strncmp(uri, "file://", 7) == 0) return uri + 7;
    return uri;
}

static OpenDoc *doc_open(const char *uri, const char *text) {
    OpenDoc *d = doc_find(uri);
    if (!d) {
        if (open_doc_count >= MAX_OPEN_DOCS) return NULL;
        d = &open_docs[open_doc_count++];
        snprintf(d->uri, sizeof(d->uri), "%s", uri);
        d->text  = NULL;
        d->state = NULL;
    }

    free(d->text);
    d->text = strdup(text);
    if (d->state) { lsp_state_free(d->state); d->state = NULL; }

    const char *path = uri_to_path(uri);

    /* lsp_analyze updates the project file and re-parses.
       Diagnostics end up in pf->diags / pf->diag_count. */
    d->state = lsp_analyze(g_project, path, text);
    return d;
}

static void doc_close(const char *uri) {
    for (int i = 0; i < open_doc_count; i++) {
        if (strcmp(open_docs[i].uri, uri) == 0) {
            free(open_docs[i].text);
            lsp_state_free(open_docs[i].state);
            for (int j = i; j < open_doc_count - 1; j++)
                open_docs[j] = open_docs[j + 1];
            open_doc_count--;
            return;
        }
    }
}

/* ─── Response / notification helpers ───────────────────────────────────────── */

static void send_result(FILE *out, long id, const char *result_json) {
    JsonBuf b;
    jb_init(&b);
    jb_obj_begin(&b);
      jb_key_str(&b, "jsonrpc", "2.0");
      jb_key_int(&b, "id", id);
      jb_key(&b, "result");
      jb_raw(&b, result_json);
      if (b.depth > 0) b.need_comma[b.depth - 1] = 1;
    jb_obj_end(&b);
    lsp_write_message(out, b.buf);
    jb_free(&b);
}

static void send_null_result(FILE *out, long id) {
    send_result(out, id, "null");
}

static void send_error(FILE *out, long id, int code, const char *message) {
    JsonBuf b;
    jb_init(&b);
    jb_obj_begin(&b);
      jb_key_str(&b, "jsonrpc", "2.0");
      jb_key_int(&b, "id", id);
      jb_key_begin_obj(&b, "error");
        jb_key_int(&b, "code", code);
        jb_key_str(&b, "message", message);
      jb_obj_end(&b);
    jb_obj_end(&b);
    lsp_write_message(out, b.buf);
    jb_free(&b);
}

static void send_notification(FILE *out, const char *method, const char *params_json) {
    JsonBuf b;
    jb_init(&b);
    jb_obj_begin(&b);
      jb_key_str(&b, "jsonrpc", "2.0");
      jb_key_str(&b, "method", method);
      jb_key(&b, "params");
      jb_raw(&b, params_json);
      if (b.depth > 0) b.need_comma[b.depth - 1] = 1;
    jb_obj_end(&b);
    lsp_write_message(out, b.buf);
    jb_free(&b);
}

/* ─── Publish diagnostics for a single file ──────────────────────────────────
   Reads from pf->diags so each file's diagnostics are independent.
   ─────────────────────────────────────────────────────────────────────────── */

static void publish_diags_for(FILE *out, const char *uri,
                               LspDiag *diags, int diag_count)
{
    JsonBuf b;
    jb_init(&b);

    jb_obj_begin(&b);
      jb_key_str(&b, "uri", uri);
      jb_key_begin_arr(&b, "diagnostics");
        for (int i = 0; i < diag_count; i++) {
            LspDiag *d = &diags[i];
            jb_arr_begin_obj(&b);
              jb_key_begin_obj(&b, "range");
                jb_key_begin_obj(&b, "start");
                  jb_key_int(&b, "line",      d->start_line);
                  jb_key_int(&b, "character", d->start_col);
                jb_obj_end(&b);
                jb_key_begin_obj(&b, "end");
                  jb_key_int(&b, "line",      d->end_line);
                  jb_key_int(&b, "character", d->end_col);
                jb_obj_end(&b);
              jb_obj_end(&b);
              jb_key_int(&b, "severity", (int)d->severity);
              jb_key_str(&b, "source",   "hylian");
              jb_key_str(&b, "message",  d->message);
            jb_obj_end(&b);
        }
      jb_arr_end(&b);
    jb_obj_end(&b);

    send_notification(out, "textDocument/publishDiagnostics", b.buf);
    jb_free(&b);
}

static void publish_diagnostics(FILE *out, const char *uri, LspState *st) {
    if (!st || !st->file) {
        /* Nothing to show — publish empty list to clear stale markers */
        LspDiag none[1];
        publish_diags_for(out, uri, none, 0);
        return;
    }
    publish_diags_for(out, uri, st->file->diags, st->file->diag_count);
}

/* ─── Handler: initialize ────────────────────────────────────────────────────── */

static void handle_initialize(FILE *out, long id, const char *params) {
    /* Extract rootUri (preferred) or rootPath (fallback) */
    char root_uri[1024]  = "";
    char root_path[1024] = ".";

    if (params) {
        int plen = (int)strlen(params);

        if (!json_get_str(params, "rootUri", root_uri, sizeof(root_uri)) ||
            root_uri[0] == '\0' ||
            strcmp(root_uri, "null") == 0) {
            /* Try rootPath */
            json_get_str(params, "rootPath", root_path, sizeof(root_path));
        } else {
            /* Strip file:// */
            const char *p = uri_to_path(root_uri);
            snprintf(root_path, sizeof(root_path), "%s", p);
        }

        /* workspaceFolders is an array — try the first entry's uri */
        int wf_len;
        const char *wf = json_get_raw(params, "workspaceFolders", &wf_len);
        if (wf && wf_len > 2) {
            /* Find first { in the array */
            const char *p = wf;
            while (*p && *p != '{') p++;
            if (*p == '{') {
                int elem_len = wf_len - (int)(p - wf);
                char wf_uri[1024] = "";
                json_get_str_in(p, elem_len, "uri", wf_uri, sizeof(wf_uri));
                if (wf_uri[0] && strcmp(wf_uri, "null") != 0) {
                    snprintf(root_path, sizeof(root_path), "%s",
                             uri_to_path(wf_uri));
                }
            }
        }

        (void)plen;
    }

    lsp_log("[lsp_main] Using workspace root: %s", root_path[0] ? root_path : ".");

    /* Create the project (scans workspace, parses all .hy files) */
    if (g_project) lsp_project_free(g_project);
    g_project = lsp_project_create(root_path[0] ? root_path : ".");

    /* Build capabilities response */
    JsonBuf b;
    jb_init(&b);

    jb_obj_begin(&b);
      jb_key_begin_obj(&b, "capabilities");

        jb_key_begin_obj(&b, "textDocumentSync");
          jb_key_bool(&b, "openClose", 1);
          jb_key_int(&b, "change", 1);   /* Full sync */
        jb_obj_end(&b);

        jb_key_bool(&b, "hoverProvider", 1);

        jb_key_begin_obj(&b, "completionProvider");
          jb_key_begin_arr(&b, "triggerCharacters");
            jb_arr_str(&b, ".");
          jb_arr_end(&b);
          jb_key_bool(&b, "resolveProvider", 0);
        jb_obj_end(&b);

      jb_obj_end(&b); /* capabilities */

      jb_key_begin_obj(&b, "serverInfo");
        jb_key_str(&b, "name",    "hylian-lsp");
        jb_key_str(&b, "version", "0.1.0");
      jb_obj_end(&b);

    jb_obj_end(&b);

    send_result(out, id, b.buf);
    jb_free(&b);
}

/* ─── Handler: textDocument/didOpen ─────────────────────────────────────────── */

static void handle_did_open(FILE *out, const char *params) {
    int plen = (int)strlen(params);

    int td_len;
    const char *td = json_get_raw_in(params, plen, "textDocument", &td_len);
    if (!td) return;

    char uri[1024] = "";
    json_get_str_in(td, td_len, "uri", uri, sizeof(uri));

    /* text is a JSON string inside textDocument */
    char text_buf[1024 * 256] = "";   /* up to 256 KB */
    json_get_str_in(td, td_len, "text", text_buf, sizeof(text_buf));

    OpenDoc *d = doc_open(uri, text_buf);
    publish_diagnostics(out, uri, d ? d->state : NULL);
}

/* ─── Handler: textDocument/didChange ───────────────────────────────────────── */

static void handle_did_change(FILE *out, const char *params) {
    int plen = (int)strlen(params);

    int td_len;
    const char *td = json_get_raw_in(params, plen, "textDocument", &td_len);
    if (!td) return;

    char uri[1024] = "";
    json_get_str_in(td, td_len, "uri", uri, sizeof(uri));

    /* contentChanges array — Full sync means [0].text is the whole file */
    int cc_len;
    const char *cc = json_get_raw_in(params, plen, "contentChanges", &cc_len);
    if (!cc) return;

    const char *p = cc;
    while (*p && *p != '{') p++;
    if (!*p) return;

    int elem_len = cc_len - (int)(p - cc);
    char new_text[1024 * 256] = "";
    json_get_str_in(p, elem_len, "text", new_text, sizeof(new_text));

    OpenDoc *d = doc_open(uri, new_text);
    publish_diagnostics(out, uri, d ? d->state : NULL);
}

/* ─── Handler: textDocument/didClose ────────────────────────────────────────── */

static void handle_did_close(FILE *out, const char *params) {
    int plen = (int)strlen(params);

    int td_len;
    const char *td = json_get_raw_in(params, plen, "textDocument", &td_len);
    if (!td) return;

    char uri[1024] = "";
    json_get_str_in(td, td_len, "uri", uri, sizeof(uri));

    doc_close(uri);

    /* Publish empty diagnostic list to clear editor markers */
    publish_diags_for(out, uri, NULL, 0);
}

/* ─── Handler: textDocument/hover ───────────────────────────────────────────── */

static void handle_hover(FILE *out, long id, const char *params) {
    int plen = (int)strlen(params);

    int td_len;
    const char *td = json_get_raw_in(params, plen, "textDocument", &td_len);
    if (!td) { send_null_result(out, id); return; }

    char uri[1024] = "";
    json_get_str_in(td, td_len, "uri", uri, sizeof(uri));

    int pos_len;
    const char *pos = json_get_raw_in(params, plen, "position", &pos_len);
    if (!pos) { send_null_result(out, id); return; }

    long line = 0, col = 0;
    json_get_int_in(pos, pos_len, "line",      &line);
    json_get_int_in(pos, pos_len, "character", &col);

    OpenDoc *doc = doc_find(uri);
    if (!doc || !doc->state || !doc->text) { send_null_result(out, id); return; }

    HoverResult hr = lsp_hover(doc->state, doc->text, (int)line, (int)col);
    if (!hr.found) { send_null_result(out, id); return; }

    JsonBuf b;
    jb_init(&b);
    jb_obj_begin(&b);
      jb_key_begin_obj(&b, "contents");
        jb_key_str(&b, "kind",  "markdown");
        jb_key_str(&b, "value", hr.content);
      jb_obj_end(&b);
    jb_obj_end(&b);

    send_result(out, id, b.buf);
    jb_free(&b);
}

/* ─── Handler: textDocument/completion ──────────────────────────────────────── */

static void handle_completion(FILE *out, long id, const char *params) {
    int plen = (int)strlen(params);

    int td_len;
    const char *td = json_get_raw_in(params, plen, "textDocument", &td_len);
    if (!td) { send_null_result(out, id); return; }

    char uri[1024] = "";
    json_get_str_in(td, td_len, "uri", uri, sizeof(uri));

    int pos_len;
    const char *pos = json_get_raw_in(params, plen, "position", &pos_len);
    if (!pos) { send_null_result(out, id); return; }

    long line = 0, col = 0;
    json_get_int_in(pos, pos_len, "line",      &line);
    json_get_int_in(pos, pos_len, "character", &col);

    OpenDoc *doc = doc_find(uri);
    if (!doc || !doc->state || !doc->text) { send_null_result(out, id); return; }

    CompletionItem items[MAX_COMPLETIONS];
    int count = lsp_complete(doc->state, doc->text, (int)line, (int)col,
                             items, MAX_COMPLETIONS);

    JsonBuf b;
    jb_init(&b);
    jb_obj_begin(&b);
      jb_key_bool(&b, "isIncomplete", 0);
      jb_key_begin_arr(&b, "items");
        for (int i = 0; i < count; i++) {
            jb_arr_begin_obj(&b);
              jb_key_str(&b, "label",  items[i].label);
              jb_key_int(&b, "kind",   (long)items[i].kind);
              if (items[i].detail[0])
                  jb_key_str(&b, "detail", items[i].detail);
              if (items[i].documentation[0]) {
                  jb_key_begin_obj(&b, "documentation");
                    jb_key_str(&b, "kind",  "markdown");
                    jb_key_str(&b, "value", items[i].documentation);
                  jb_obj_end(&b);
              }
            jb_obj_end(&b);
        }
      jb_arr_end(&b);
    jb_obj_end(&b);

    send_result(out, id, b.buf);
    jb_free(&b);
}

/* ─── Main dispatch loop ─────────────────────────────────────────────────────── */

int main(void) {
#ifdef _WIN32
    _setmode(_fileno(stdin),  0x8000);
    _setmode(_fileno(stdout), 0x8000);
#endif

    FILE *in  = stdin;
    FILE *out = stdout;

    int initialized    = 0;
    int shutdown_recvd = 0;

    /* Bootstrap a minimal project so we can handle files before initialize
       (some clients are fast). */
    g_project = lsp_project_create(".");

    for (;;) {
        char *msg = lsp_read_message(in);
        if (!msg) break;

        char method[128] = "";
        long id          = -1;
        int  has_id      = json_get_int(msg, "id", &id);
        json_get_str(msg, "method", method, sizeof(method));

        int params_len = 0;
        const char *params_raw = json_get_raw(msg, "params", &params_len);
        char *params = NULL;
        if (params_raw && params_len > 0) {
            params = malloc(params_len + 1);
            memcpy(params, params_raw, params_len);
            params[params_len] = '\0';
        }

        /* ── Lifecycle ── */

        if (strcmp(method, "initialize") == 0) {
            handle_initialize(out, id, params);
            initialized = 1;

        } else if (strcmp(method, "initialized") == 0) {
            /* notification — no response */

        } else if (strcmp(method, "shutdown") == 0) {
            shutdown_recvd = 1;
            send_null_result(out, id);

        } else if (strcmp(method, "exit") == 0) {
            free(params);
            free(msg);
            if (g_project) lsp_project_free(g_project);
            return shutdown_recvd ? 0 : 1;

        } else if (!initialized) {
            if (has_id)
                send_error(out, id, -32002, "server not yet initialized");

        /* ── Text document sync ── */

        } else if (strcmp(method, "textDocument/didOpen") == 0) {
            if (params) handle_did_open(out, params);

        } else if (strcmp(method, "textDocument/didChange") == 0) {
            if (params) handle_did_change(out, params);

        } else if (strcmp(method, "textDocument/didClose") == 0) {
            if (params) handle_did_close(out, params);

        /* ── Language features ── */

        } else if (strcmp(method, "textDocument/hover") == 0) {
            if (params) handle_hover(out, id, params);
            else send_null_result(out, id);

        } else if (strcmp(method, "textDocument/completion") == 0) {
            if (params) handle_completion(out, id, params);
            else send_null_result(out, id);

        /* ── Unknown ── */

        } else {
            if (has_id)
                send_error(out, id, -32601, "method not found");
        }

        free(params);
        free(msg);
    }

    if (g_project) lsp_project_free(g_project);
    return 0;
}