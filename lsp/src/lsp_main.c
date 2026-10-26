#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <poll.h>
#include <unistd.h>

#include "lsp_proto.h"
#include "lsp_diag.h"
#include "lsp_log.h"
#include "lsp_analysis.h"
#include "diag.h"

/* One project (module cache) for the whole session. Which Zora project a FILE
   belongs to is decided per file by walking up to its Zora.toml - the folder
   the editor opened only seeds the cache. */
static LspProject *g_project = NULL;

#define MAX_OPEN_DOCS 64

typedef struct {
    char      uri[1024];
    char      path[1024];
    char     *text;       /* current buffer */
    LspState *state;      /* analysis of `text`; NULL when stale */
    int       dirty;      /* text changed since `state` was built */
    int       published;  /* diagnostics for this state were sent */
} OpenDoc;

static OpenDoc open_docs[MAX_OPEN_DOCS];
static int     open_doc_count = 0;

static OpenDoc *doc_find(const char *uri) {
    for (int i = 0; i < open_doc_count; i++)
        if (strcmp(open_docs[i].uri, uri) == 0) return &open_docs[i];
    return NULL;
}

/* file:///a%20b/c.hy -> /a b/c.hy */
static void uri_to_path(const char *uri, char *out, size_t n) {
    const char *p = strncmp(uri, "file://", 7) == 0 ? uri + 7 : uri;
    size_t o = 0;
    for (; *p && o + 1 < n; p++) {
        if (*p == '%' && isxdigit((unsigned char)p[1]) && isxdigit((unsigned char)p[2])) {
            char hex[3] = { p[1], p[2], 0 };
            out[o++] = (char)strtol(hex, NULL, 16);
            p += 2;
        } else out[o++] = *p;
    }
    out[o] = '\0';
}

static void path_to_uri(const char *path, char *out, size_t n) {
    size_t o = (size_t)snprintf(out, n, "file://");
    for (; *path && o + 4 < n; path++) {
        unsigned char c = (unsigned char)*path;
        if (isalnum(c) || strchr("/-_.~", c)) out[o++] = (char)c;
        else o += (size_t)snprintf(out + o, n - o, "%%%02X", c);
    }
    out[o] = '\0';
}

static OpenDoc *doc_open(const char *uri, const char *text) {
    OpenDoc *d = doc_find(uri);
    if (!d) {
        if (open_doc_count >= MAX_OPEN_DOCS) return NULL;
        d = &open_docs[open_doc_count++];
        memset(d, 0, sizeof(*d));
        snprintf(d->uri, sizeof(d->uri), "%s", uri);
        uri_to_path(uri, d->path, sizeof(d->path));
    }
    free(d->text);
    d->text = strdup(text);
    d->dirty = 1;
    d->published = 0;
    /* other files that include this one must see the new text right away */
    lsp_project_set_overlay(g_project, d->path, d->text);
    return d;
}

static void doc_close(const char *uri) {
    for (int i = 0; i < open_doc_count; i++) {
        if (strcmp(open_docs[i].uri, uri) == 0) {
            lsp_project_set_overlay(g_project, open_docs[i].path, NULL);
            free(open_docs[i].text);
            lsp_state_free(open_docs[i].state);
            for (int j = i; j < open_doc_count - 1; j++) open_docs[j] = open_docs[j + 1];
            open_doc_count--;
            return;
        }
    }
}

/* bring a document's analysis up to date */
static LspState *doc_state(OpenDoc *d) {
    if (!d || !d->text) return NULL;
    if (d->dirty || !d->state) {
        lsp_state_free(d->state);
        d->state = lsp_analyze(g_project, d->path, d->text);
        d->dirty = 0;
        d->published = 0;
    }
    return d->state;
}

/* ── output ──────────────────────────────────────────────────────────────── */

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

static void send_null_result(FILE *out, long id) { send_result(out, id, "null"); }

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

static void put_range(JsonBuf *b, int l1, int c1, int l2, int c2) {
    jb_key_begin_obj(b, "range");
      jb_key_begin_obj(b, "start");
        jb_key_int(b, "line", l1); jb_key_int(b, "character", c1);
      jb_obj_end(b);
      jb_key_begin_obj(b, "end");
        jb_key_int(b, "line", l2); jb_key_int(b, "character", c2);
      jb_obj_end(b);
    jb_obj_end(b);
}

static void publish_diags(FILE *out, const char *uri, const LspDiag *diags, int count) {
    JsonBuf b;
    jb_init(&b);
    jb_obj_begin(&b);
      jb_key_str(&b, "uri", uri);
      jb_key_begin_arr(&b, "diagnostics");
        for (int i = 0; i < count; i++) {
            const LspDiag *d = &diags[i];
            jb_arr_begin_obj(&b);
              put_range(&b, d->start_line, d->start_col, d->end_line, d->end_col);
              jb_key_int(&b, "severity", (int)d->severity);
              jb_key_str(&b, "source", "hylian");
              jb_key_str(&b, "message", d->message);
            jb_obj_end(&b);
        }
      jb_arr_end(&b);
    jb_obj_end(&b);
    send_notification(out, "textDocument/publishDiagnostics", b.buf);
    jb_free(&b);
}

static void publish_doc(FILE *out, OpenDoc *d) {
    LspState *st = doc_state(d);
    int n = 0;
    const LspDiag *diags = st ? lsp_state_diags(st, &n) : NULL;
    publish_diags(out, d->uri, diags, n);
    d->published = 1;
}

/* ── requests ────────────────────────────────────────────────────────────── */

static void handle_initialize(FILE *out, long id, const char *params) {
    char root_uri[1024] = "", root_path[1024] = ".";
    if (params) {
        if (!json_get_str(params, "rootUri", root_uri, sizeof(root_uri)) || !root_uri[0] ||
            strcmp(root_uri, "null") == 0) {
            json_get_str(params, "rootPath", root_path, sizeof(root_path));
        } else {
            uri_to_path(root_uri, root_path, sizeof(root_path));
        }
        int wf_len;
        const char *wf = json_get_raw(params, "workspaceFolders", &wf_len);
        if (wf && wf_len > 2) {
            const char *p = wf;
            while (*p && *p != '{') p++;
            if (*p == '{') {
                char wf_uri[1024] = "";
                json_get_str_in(p, wf_len - (int)(p - wf), "uri", wf_uri, sizeof(wf_uri));
                if (wf_uri[0] && strcmp(wf_uri, "null") != 0) uri_to_path(wf_uri, root_path, sizeof(root_path));
            }
        }
    }
    lsp_log("[lsp_main] workspace root: %s", root_path);
    if (g_project) lsp_project_free(g_project);
    g_project = lsp_project_create(root_path);

    JsonBuf b;
    jb_init(&b);
    jb_obj_begin(&b);
      jb_key_begin_obj(&b, "capabilities");
        jb_key_begin_obj(&b, "textDocumentSync");
          jb_key_bool(&b, "openClose", 1);
          jb_key_int(&b, "change", 1);
        jb_obj_end(&b);
        jb_key_bool(&b, "hoverProvider", 1);
        jb_key_bool(&b, "definitionProvider", 1);
        jb_key_bool(&b, "documentSymbolProvider", 1);
        jb_key_begin_obj(&b, "completionProvider");
          jb_key_begin_arr(&b, "triggerCharacters");
            jb_arr_str(&b, ".");
          jb_arr_end(&b);
          jb_key_bool(&b, "resolveProvider", 0);
        jb_obj_end(&b);
        jb_key_begin_obj(&b, "signatureHelpProvider");
          jb_key_begin_arr(&b, "triggerCharacters");
            jb_arr_str(&b, "(");
            jb_arr_str(&b, ",");
          jb_arr_end(&b);
        jb_obj_end(&b);
      jb_obj_end(&b);
      jb_key_begin_obj(&b, "serverInfo");
        jb_key_str(&b, "name", "hylian-lsp");
        jb_key_str(&b, "version", "0.2.0");
      jb_obj_end(&b);
    jb_obj_end(&b);
    send_result(out, id, b.buf);
    jb_free(&b);
}

static int read_uri(const char *params, char *uri, size_t n) {
    int plen = (int)strlen(params), td_len;
    const char *td = json_get_raw_in(params, plen, "textDocument", &td_len);
    if (!td) return 0;
    return json_get_str_in(td, td_len, "uri", uri, (int)n);
}

static int read_position(const char *params, long *line, long *col) {
    int plen = (int)strlen(params), pos_len;
    const char *pos = json_get_raw_in(params, plen, "position", &pos_len);
    if (!pos) return 0;
    *line = 0; *col = 0;
    json_get_int_in(pos, pos_len, "line", line);
    json_get_int_in(pos, pos_len, "character", col);
    return 1;
}

static void handle_did_open(FILE *out, const char *params) {
    int plen = (int)strlen(params), td_len;
    const char *td = json_get_raw_in(params, plen, "textDocument", &td_len);
    if (!td) return;
    char uri[1024] = "";
    json_get_str_in(td, td_len, "uri", uri, sizeof(uri));
    /* buffers can be large: read straight into the heap */
    char *text = malloc((size_t)td_len + 1);
    text[0] = '\0';
    json_get_str_in(td, td_len, "text", text, td_len + 1);
    OpenDoc *d = doc_open(uri, text);
    free(text);
    if (d) publish_doc(out, d);
}

/* didChange only records the text. Analysis waits until the client stops
   sending (or asks a question), so typing a word costs one parse, not one per
   keystroke. */
static void handle_did_change(const char *params) {
    int plen = (int)strlen(params);
    char uri[1024] = "";
    if (!read_uri(params, uri, sizeof(uri))) return;
    int cc_len;
    const char *cc = json_get_raw_in(params, plen, "contentChanges", &cc_len);
    if (!cc) return;
    const char *p = cc;
    while (*p && *p != '{') p++;
    if (!*p) return;
    int elem_len = cc_len - (int)(p - cc);
    char *text = malloc((size_t)elem_len + 1);
    text[0] = '\0';
    json_get_str_in(p, elem_len, "text", text, elem_len + 1);
    doc_open(uri, text);
    free(text);
}

static void handle_did_close(FILE *out, const char *params) {
    char uri[1024] = "";
    if (!read_uri(params, uri, sizeof(uri))) return;
    doc_close(uri);
    publish_diags(out, uri, NULL, 0);
}

static void handle_watched_files(const char *params) {
    /* a Zora.toml or a vendor interface changed on disk */
    (void)params;
    lsp_project_file_changed(g_project, "");
    for (int i = 0; i < open_doc_count; i++) open_docs[i].dirty = 1;
}

static void handle_hover(FILE *out, long id, const char *params) {
    char uri[1024] = ""; long line, col;
    if (!read_uri(params, uri, sizeof(uri)) || !read_position(params, &line, &col)) { send_null_result(out, id); return; }
    LspState *st = doc_state(doc_find(uri));
    if (!st) { send_null_result(out, id); return; }
    HoverResult hr = lsp_hover(st, (int)line, (int)col);
    if (!hr.found) { send_null_result(out, id); return; }
    JsonBuf b;
    jb_init(&b);
    jb_obj_begin(&b);
      jb_key_begin_obj(&b, "contents");
        jb_key_str(&b, "kind", "markdown");
        jb_key_str(&b, "value", hr.content);
      jb_obj_end(&b);
    jb_obj_end(&b);
    send_result(out, id, b.buf);
    jb_free(&b);
}

static void handle_definition(FILE *out, long id, const char *params) {
    char uri[1024] = ""; long line, col;
    if (!read_uri(params, uri, sizeof(uri)) || !read_position(params, &line, &col)) { send_null_result(out, id); return; }
    LspState *st = doc_state(doc_find(uri));
    if (!st) { send_null_result(out, id); return; }
    DefLocation d = lsp_definition(st, (int)line, (int)col);
    if (!d.found) { send_null_result(out, id); return; }
    char target[2200];
    path_to_uri(d.path, target, sizeof(target));
    JsonBuf b;
    jb_init(&b);
    jb_obj_begin(&b);
      jb_key_str(&b, "uri", target);
      put_range(&b, d.line, d.col, d.line, d.end_col);
    jb_obj_end(&b);
    send_result(out, id, b.buf);
    jb_free(&b);
}

static void handle_signature_help(FILE *out, long id, const char *params) {
    char uri[1024] = ""; long line, col;
    if (!read_uri(params, uri, sizeof(uri)) || !read_position(params, &line, &col)) { send_null_result(out, id); return; }
    LspState *st = doc_state(doc_find(uri));
    if (!st) { send_null_result(out, id); return; }
    SignatureInfo si = lsp_signature_help(st, (int)line, (int)col);
    if (!si.found) { send_null_result(out, id); return; }
    JsonBuf b;
    jb_init(&b);
    jb_obj_begin(&b);
      jb_key_begin_arr(&b, "signatures");
        jb_arr_begin_obj(&b);
          jb_key_str(&b, "label", si.label);
          jb_key_begin_arr(&b, "parameters");
            for (int i = 0; i < si.param_count; i++) {
                jb_arr_begin_obj(&b);
                  jb_key_begin_arr(&b, "label");
                    jb_arr_int(&b, si.param_start[i]);
                    jb_arr_int(&b, si.param_end[i]);
                  jb_arr_end(&b);
                jb_obj_end(&b);
            }
          jb_arr_end(&b);
        jb_obj_end(&b);
      jb_arr_end(&b);
      jb_key_int(&b, "activeSignature", 0);
      jb_key_int(&b, "activeParameter", si.active);
    jb_obj_end(&b);
    send_result(out, id, b.buf);
    jb_free(&b);
}

static void emit_symbol(JsonBuf *b, const SymbolInfo *v, int n, int i) {
    const SymbolInfo *s = &v[i];
    jb_arr_begin_obj(b);
      jb_key_str(b, "name", s->name);
      jb_key_str(b, "detail", s->detail);
      jb_key_int(b, "kind", s->kind);
      put_range(b, s->line, s->col, s->end_line, s->end_col);
      /* selectionRange must sit inside range */
      {
          int sc = s->name_col, ec = s->name_col + (int)strlen(s->name);
          jb_key_begin_obj(b, "selectionRange");
            jb_key_begin_obj(b, "start"); jb_key_int(b, "line", s->line); jb_key_int(b, "character", sc); jb_obj_end(b);
            jb_key_begin_obj(b, "end");   jb_key_int(b, "line", s->line); jb_key_int(b, "character", ec); jb_obj_end(b);
          jb_obj_end(b);
      }
      jb_key_begin_arr(b, "children");
        for (int c = 0; c < n; c++) if (v[c].parent == i) emit_symbol(b, v, n, c);
      jb_arr_end(b);
    jb_obj_end(b);
}

static void handle_document_symbols(FILE *out, long id, const char *params) {
    char uri[1024] = "";
    if (!read_uri(params, uri, sizeof(uri))) { send_null_result(out, id); return; }
    LspState *st = doc_state(doc_find(uri));
    int n = 0;
    SymbolInfo *syms = st ? lsp_document_symbols(st, &n) : NULL;
    JsonBuf b;
    jb_init(&b);
    jb_arr_begin(&b);
      for (int i = 0; i < n; i++) if (syms[i].parent < 0) emit_symbol(&b, syms, n, i);
    jb_arr_end(&b);
    send_result(out, id, b.buf);
    jb_free(&b);
    free(syms);
}

/* Where an item's text replaces what was typed: identifiers replace the word
   before the cursor; dotted include paths replace the whole dotted run (else
   `std.` + `std.os.exec` would insert `std.std.os.exec`). */
static int replace_start(const char *text, int line, int col, int dotted) {
    int ls = 0, cur = 0;
    if (line > 0) {
        int found = 0;
        for (const char *p = text; *p; p++)
            if (*p == '\n' && ++cur == line) { ls = (int)(p - text) + 1; found = 1; break; }
        if (!found) return col;
    }
    int len = 0;
    while (text[ls + len] && text[ls + len] != '\n') len++;
    if (col > len) col = len;
    int i = col;
    while (i > 0) {
        char c = text[ls + i - 1];
        if (!(isalnum((unsigned char)c) || c == '_' || (dotted && c == '.'))) break;
        i--;
    }
    return i;
}

static void handle_completion(FILE *out, long id, const char *params) {
    char uri[1024] = ""; long line, col;
    if (!read_uri(params, uri, sizeof(uri)) || !read_position(params, &line, &col)) { send_null_result(out, id); return; }
    OpenDoc *doc = doc_find(uri);
    LspState *st = doc_state(doc);
    if (!st) { send_null_result(out, id); return; }

    int count = 0, incomplete = 0;
    CompletionItem *items = lsp_complete(st, (int)line, (int)col, &count, &incomplete);

    JsonBuf b;
    jb_init(&b);
    jb_obj_begin(&b);
      jb_key_bool(&b, "isIncomplete", incomplete);
      jb_key_begin_arr(&b, "items");
        for (int i = 0; i < count; i++) {
            const CompletionItem *it = &items[i];
            jb_arr_begin_obj(&b);
              jb_key_str(&b, "label", it->label);
              jb_key_int(&b, "kind", (long)it->kind);
              jb_key_str(&b, "sortText", it->sort_key[0] ? it->sort_key : "5");
              {
                  int start = replace_start(doc->text, (int)line, (int)col, strchr(it->label, '.') != NULL);
                  jb_key_begin_obj(&b, "textEdit");
                    put_range(&b, (int)line, start, (int)line, (int)col);
                    jb_key_str(&b, "newText", it->insert_text[0] ? it->insert_text : it->label);
                  jb_obj_end(&b);
              }
              if (it->is_snippet) jb_key_int(&b, "insertTextFormat", 2);
              if (it->detail[0]) jb_key_str(&b, "detail", it->detail);
              if (it->documentation[0]) {
                  jb_key_begin_obj(&b, "documentation");
                    jb_key_str(&b, "kind", "markdown");
                    jb_key_str(&b, "value", it->documentation);
                  jb_obj_end(&b);
              }
              if (it->has_import) {
                  jb_key_begin_arr(&b, "additionalTextEdits");
                    jb_arr_begin_obj(&b);
                      put_range(&b, it->import_line, it->import_col, it->import_line, it->import_col);
                      jb_key_str(&b, "newText", it->import_text);
                    jb_obj_end(&b);
                  jb_arr_end(&b);
              }
            jb_obj_end(&b);
        }
      jb_arr_end(&b);
    jb_obj_end(&b);
    send_result(out, id, b.buf);
    jb_free(&b);
    free(items);
}

/* Is more input already waiting? Then a keystroke burst is still arriving and
   analysing now would be wasted work. */
static int input_pending(void) {
#ifdef __GLIBC__
    if (stdin->_IO_read_ptr < stdin->_IO_read_end) return 1;
#endif
    struct pollfd pf = { 0, POLLIN, 0 };
    return poll(&pf, 1, 0) > 0;
}

/* analyse and publish whatever changed, once the input has gone quiet */
static void flush_dirty(FILE *out) {
    if (input_pending()) return;
    for (int i = 0; i < open_doc_count; i++)
        if (open_docs[i].dirty || !open_docs[i].published) publish_doc(out, &open_docs[i]);
}

int main(void) {
    FILE *in = stdin, *out = stdout;
    int initialized = 0, shutdown_recvd = 0;

    g_project = lsp_project_create(".");

    for (;;) {
        char *msg = lsp_read_message(in);
        if (!msg) break;

        char method[128] = "";
        long id = -1;
        int has_id = json_get_int(msg, "id", &id);
        json_get_str(msg, "method", method, sizeof(method));

        int params_len = 0;
        const char *params_raw = json_get_raw(msg, "params", &params_len);
        char *params = NULL;
        if (params_raw && params_len > 0) {
            params = malloc((size_t)params_len + 1);
            memcpy(params, params_raw, (size_t)params_len);
            params[params_len] = '\0';
        }

        if (strcmp(method, "initialize") == 0) {
            handle_initialize(out, id, params);
            initialized = 1;
        } else if (strcmp(method, "initialized") == 0) {
        } else if (strcmp(method, "shutdown") == 0) {
            shutdown_recvd = 1;
            send_null_result(out, id);
        } else if (strcmp(method, "exit") == 0) {
            free(params); free(msg);
            if (g_project) lsp_project_free(g_project);
            return shutdown_recvd ? 0 : 1;
        } else if (!initialized) {
            if (has_id) send_error(out, id, -32002, "server not yet initialized");
        } else if (strcmp(method, "textDocument/didOpen") == 0) {
            if (params) handle_did_open(out, params);
        } else if (strcmp(method, "textDocument/didChange") == 0) {
            if (params) handle_did_change(params);
        } else if (strcmp(method, "textDocument/didClose") == 0) {
            if (params) handle_did_close(out, params);
        } else if (strcmp(method, "workspace/didChangeWatchedFiles") == 0) {
            handle_watched_files(params);
        } else if (strcmp(method, "textDocument/hover") == 0) {
            if (params) handle_hover(out, id, params); else send_null_result(out, id);
        } else if (strcmp(method, "textDocument/completion") == 0) {
            if (params) handle_completion(out, id, params); else send_null_result(out, id);
        } else if (strcmp(method, "textDocument/definition") == 0) {
            if (params) handle_definition(out, id, params); else send_null_result(out, id);
        } else if (strcmp(method, "textDocument/signatureHelp") == 0) {
            if (params) handle_signature_help(out, id, params); else send_null_result(out, id);
        } else if (strcmp(method, "textDocument/documentSymbol") == 0) {
            if (params) handle_document_symbols(out, id, params); else send_null_result(out, id);
        } else if (has_id) {
            send_error(out, id, -32601, "method not found");
        }

        free(params);
        free(msg);
        if (initialized) flush_dirty(out);
    }

    if (g_project) lsp_project_free(g_project);
    return 0;
}
