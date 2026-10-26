/* lsp_features.c - hover, go-to-definition, signature help, document symbols. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "lsp_internal.h"

/* ── symbol under the cursor ─────────────────────────────────────────────── */

typedef struct {
    int  found;
    char sig[512];       /* declaration as written, e.g. `int add(int a, int b)` */
    char kindname[32];   /* "function", "class", "variable", ... */
    char doc[512];
    char path[1024];
    int  line, col, end_col;
} Sym;

static const char *display_path(LspState *st, const char *path) {
    const char *root = st->zora.root;
    size_t rl = strlen(root);
    if (rl && !strncmp(path, root, rl) && path[rl] == '/') return path + rl + 1;
    return path;
}

/* place the symbol at the name inside its declaration's first line */
static void place(Sym *s, LspState *st, const ASTNode *node, Module *from, const char *name) {
    const char *text = from ? from->text : st->text;
    snprintf(s->path, sizeof(s->path), "%s", from ? from->path : st->path);
    s->line = node->line > 0 ? node->line - 1 : 0;
    s->col = node->col;
    s->end_col = node->col + (int)strlen(name);
    if (text) {
        int ls, le;
        if (lq_line_bounds(text, s->line, &ls, &le)) {
            size_t nl = strlen(name);
            for (int i = node->col > 0 ? node->col : 0; i + (int)nl <= le - ls; i++) {
                if (strncmp(text + ls + i, name, nl) != 0) continue;
                if (i > 0 && lq_is_ident(text[ls + i - 1])) continue;
                if (lq_is_ident(text[ls + i + nl])) continue;
                s->col = i; s->end_col = i + (int)nl;
                break;
            }
        }
        lq_doc_above(text, s->line, s->doc, sizeof(s->doc));
    }
}

static void sym_func(Sym *s, LspState *st, FuncNode *f, Module *from) {
    s->found = 1;
    snprintf(s->kindname, sizeof(s->kindname), "function");
    lq_func_sig(f->name, f->return_type, f->params, f->param_count, s->sig, sizeof(s->sig));
    place(s, st, &f->base, from, f->name);
}

static void sym_class(Sym *s, LspState *st, ClassNode *c, Module *from) {
    s->found = 1;
    snprintf(s->kindname, sizeof(s->kindname), "%s", c->has_ctor ? "class" : "struct");
    size_t o = (size_t)snprintf(s->sig, sizeof(s->sig), "%s %s", c->has_ctor ? "class" : "struct", c->name);
    if (c->field_count > 0 && o + 4 < sizeof(s->sig)) {
        o += (size_t)snprintf(s->sig + o, sizeof(s->sig) - o, " {");
        for (int i = 0; i < c->field_count && i < 12 && o + 40 < sizeof(s->sig); i++) {
            FieldNode *f = c->fields[i];
            if (!f) continue;
            char tn[96];
            lq_type_str(f->field_type, tn, sizeof(tn));
            o += (size_t)snprintf(s->sig + o, sizeof(s->sig) - o, "\n    %s %s", tn, f->name);
        }
        if (c->field_count > 12) o += (size_t)snprintf(s->sig + o, sizeof(s->sig) - o, "\n    ...");
        snprintf(s->sig + o, sizeof(s->sig) - o, "\n}");
    }
    place(s, st, &c->base, from, c->name);
}

static void sym_field(Sym *s, LspState *st, ClassNode *c, FieldNode *f, Module *from) {
    s->found = 1;
    snprintf(s->kindname, sizeof(s->kindname), "field");
    char tn[96];
    lq_type_str(f->field_type, tn, sizeof(tn));
    snprintf(s->sig, sizeof(s->sig), "%s %s.%s", tn, c->name, f->name);
    place(s, st, f->base.line > 0 ? &f->base : &c->base, from, f->name);
}

static void sym_method(Sym *s, LspState *st, ClassNode *c, MethodNode *m, Module *from) {
    s->found = 1;
    snprintf(s->kindname, sizeof(s->kindname), "method");
    char sig[300];
    lq_func_sig(m->name, m->return_type, m->params, m->param_count, sig, sizeof(sig));
    snprintf(s->sig, sizeof(s->sig), "%s   (in %s)", sig, c->name);
    place(s, st, m->base.line > 0 ? &m->base : &c->base, from, m->name);
}

static void sym_var(Sym *s, LspState *st, const ScopeVar *v) {
    s->found = 1;
    snprintf(s->kindname, sizeof(s->kindname), "%s", v->is_param ? "parameter" : "variable");
    char tn[96];
    lq_type_str(v->type, tn, sizeof(tn));
    snprintf(s->sig, sizeof(s->sig), "%s %s", tn, v->name);
    snprintf(s->path, sizeof(s->path), "%s", st->path);
    s->line = v->line > 0 ? v->line - 1 : 0;
    s->col = v->col; s->end_col = v->col + (int)strlen(v->name);
    int ls, le;
    if (lq_line_bounds(st->text, s->line, &ls, &le)) {
        size_t nl = strlen(v->name);
        for (int i = v->col; i + (int)nl <= le - ls; i++)
            if (!strncmp(st->text + ls + i, v->name, nl) && !lq_is_ident(st->text[ls + i + nl]) &&
                (i == 0 || !lq_is_ident(st->text[ls + i - 1]))) { s->col = i; s->end_col = i + (int)nl; break; }
    }
}

static const char *KEYWORD_HELP[][2] = {
    {"if", "Conditional: `if (cond) { ... } else { ... }`"},
    {"while", "Loop while a condition holds"},
    {"for", "`for (i in 0..n)`, `for (x in array)`, or a C-style `for (init; cond; step)`"},
    {"defer", "Run a statement when the function returns (in reverse order of `defer`s)"},
    {"unsafe", "Block where raw pointer dereference and inline asm are allowed"},
    {"new", "Allocate a class instance on the arena"},
    {"cast", "`cast<T>(x)` converts between types"},
    {"addrof_fn", "`addrof_fn(f)` is the address of function `f` (store it in a `usize`, call it like `fp(args)`)"},
    {"nil", "The null value"},
    {"static", "File-private function, or a global variable"},
    {"int", "64-bit signed integer"}, {"str", "Byte string (pointer to NUL-terminated bytes)"},
    {"bool", "`true` or `false`"}, {"float", "64-bit floating point (C `double`)"},
    {"float32", "32-bit floating point (C `float`)"}, {"usize", "Pointer-sized unsigned integer"},
    {"Error", "Error value: `Err(\"message\")`, with `.message()` and `.code()`"},
    {NULL, NULL}
};

static void resolve_symbol(LspState *st, int line, int col, Sym *out) {
    memset(out, 0, sizeof(*out));
    const char *t = st->text;
    int off = lq_offset(t, line, col);
    if (lq_lex_state(t, off) != 0) return;

    int ws = off, we = off;
    while (ws > 0 && lq_is_ident(t[ws - 1])) ws--;
    while (t[we] && lq_is_ident(t[we])) we++;
    if (ws == we) return;
    char name[128];
    int nl = we - ws < 127 ? we - ws : 127;
    memcpy(name, t + ws, (size_t)nl); name[nl] = '\0';

    Scope sc;
    lq_scope_at(st, line, col, &sc);

    /* member access: `recv.name` */
    if (ws > 0 && t[ws - 1] == '.' && !(ws > 1 && t[ws - 2] == '.')) {
        int cs = lq_chain_start(t, ws - 1);
        Resolved r = lq_resolve_chain(st, &sc, t, cs, ws - 1);
        if (r.kind == RES_ENUM) {
            EnumNode *en = lq_find_enum(st, r.name, NULL);
            Module *from = NULL;
            en = lq_find_enum(st, r.name, &from);
            for (int i = 0; en && i < en->variant_count; i++)
                if (!strcmp(en->variants[i].name, name)) {
                    out->found = 1;
                    snprintf(out->kindname, sizeof(out->kindname), "enum member");
                    snprintf(out->sig, sizeof(out->sig), "%s.%s = %d", en->name, name, en->variants[i].value);
                    place(out, st, &en->base, from, name);
                    return;
                }
        } else if (r.kind == RES_MODULE) {
            Module *from = NULL;
            ModuleNode *m = lq_find_module(st, r.name, &from);
            for (int f = 0; m && f < m->func_count; f++)
                if (m->funcs[f] && !strcmp(m->funcs[f]->name, name)) { sym_func(out, st, m->funcs[f], from); return; }
        } else if (r.kind == RES_TYPE && r.type.kind == TYPE_SIMPLE && r.type.name) {
            Module *from = NULL;
            ClassNode *c = lq_find_class(st, r.type.name, &from);
            if (c) {
                FieldNode *f = lq_find_field(c, name);
                if (f) { sym_field(out, st, c, f, from); return; }
                MethodNode *m = lq_find_method(c, name);
                if (m) { sym_method(out, st, c, m, from); return; }
            }
            Module *ffrom = NULL;
            FuncNode *fn = lq_find_func(st, name, &ffrom);   /* uniform call syntax */
            if (fn) { sym_func(out, st, fn, ffrom); return; }
        }
        return;
    }

    /* locals and parameters shadow everything */
    for (int i = sc.count - 1; i >= 0; i--)
        if (!strcmp(sc.vars[i].name, name)) { sym_var(out, st, &sc.vars[i]); return; }

    if (sc.cls) {
        FieldNode *f = lq_find_field(sc.cls, name);
        if (f) { sym_field(out, st, sc.cls, f, NULL); return; }
        MethodNode *m = lq_find_method(sc.cls, name);
        if (m) { sym_method(out, st, sc.cls, m, NULL); return; }
    }

    Module *from = NULL;
    FuncNode *f = lq_find_func(st, name, &from);
    if (f) { sym_func(out, st, f, from); return; }
    ClassNode *c = lq_find_class(st, name, &from);
    if (c) { sym_class(out, st, c, from); return; }
    EnumNode *en = lq_find_enum(st, name, &from);
    if (en) {
        out->found = 1;
        snprintf(out->kindname, sizeof(out->kindname), "enum");
        size_t o = (size_t)snprintf(out->sig, sizeof(out->sig), "enum %s { ", en->name);
        for (int i = 0; i < en->variant_count && i < 8 && o + 24 < sizeof(out->sig); i++)
            o += (size_t)snprintf(out->sig + o, sizeof(out->sig) - o, "%s%s", i ? ", " : "", en->variants[i].name);
        snprintf(out->sig + o, sizeof(out->sig) - o, "%s }", en->variant_count > 8 ? ", ..." : "");
        place(out, st, &en->base, from, en->name);
        return;
    }
    ModuleNode *mn = lq_find_module(st, name, &from);
    if (mn) {
        out->found = 1;
        snprintf(out->kindname, sizeof(out->kindname), "module");
        snprintf(out->sig, sizeof(out->sig), "module %s", mn->name);
        place(out, st, &mn->base, from, mn->name);
        return;
    }
    StaticVarNode *sv = lq_find_static(st, name, &from);
    if (sv) {
        out->found = 1;
        snprintf(out->kindname, sizeof(out->kindname), "%s", sv->is_const ? "constant" : "global");
        char tn[96];
        lq_type_str(sv->var_type, tn, sizeof(tn));
        snprintf(out->sig, sizeof(out->sig), "%s%s %s", sv->is_const ? "const " : "", tn, sv->var_name);
        place(out, st, &sv->base, from, sv->var_name);
        return;
    }
    for (int i = 0; KEYWORD_HELP[i][0]; i++)
        if (!strcmp(KEYWORD_HELP[i][0], name)) {
            out->found = 1;
            snprintf(out->kindname, sizeof(out->kindname), "keyword");
            snprintf(out->sig, sizeof(out->sig), "%s", name);
            snprintf(out->doc, sizeof(out->doc), "%s", KEYWORD_HELP[i][1]);
            out->path[0] = '\0';
            return;
        }
}

/* ── include lines ───────────────────────────────────────────────────────── */

/* the dotted include path under the cursor, if the cursor is in `include { }` */
static int include_path_at(const char *t, int off, char *out, size_t n) {
    int i = off, j = off;
    while (i > 0 && (lq_is_ident(t[i - 1]) || t[i - 1] == '.')) i--;
    while (t[j] && (lq_is_ident(t[j]) || t[j] == '.')) j++;
    if (i == j) return 0;
    /* between the braces of an include? */
    int k = i - 1, ok = 0;
    for (; k >= 0; k--) {
        if (t[k] == '}' || t[k] == ';') return 0;
        if (t[k] == '{') {
            int m = k - 1;
            while (m >= 0 && isspace((unsigned char)t[m])) m--;
            ok = m >= 6 && !strncmp(t + m - 6, "include", 7);
            break;
        }
    }
    if (!ok) return 0;
    size_t len = (size_t)(j - i) < n - 1 ? (size_t)(j - i) : n - 1;
    memcpy(out, t + i, len);
    out[len] = '\0';
    return 1;
}

/* ── hover ───────────────────────────────────────────────────────────────── */

HoverResult lsp_hover(LspState *st, int line, int col) {
    HoverResult hr = {0};
    int off = lq_offset(st->text, line, col);

    char inc[128];
    if (include_path_at(st->text, off, inc, sizeof(inc))) {
        ResolvedInclude ri;
        char *fd = dir_of(st->path);
        loader_resolve_include(inc, st->zora.src_dir, fd, &ri);
        free(fd);
        const char *file = ri.hy && module_exists(ri.hy) ? ri.hy : ri.hyi;
        hr.found = 1;
        if (file) snprintf(hr.content, sizeof(hr.content), "**module** `%s`\n\n`%s`", inc, display_path(st, file));
        else      snprintf(hr.content, sizeof(hr.content), "**module** `%s`\n\n*not found*", inc);
        loader_free_resolved(&ri);
        return hr;
    }

    Sym s;
    resolve_symbol(st, line, col, &s);
    if (!s.found) return hr;
    hr.found = 1;
    size_t o = (size_t)snprintf(hr.content, sizeof(hr.content), "```hylian\n%s\n```", s.sig);
    if (s.doc[0] && o + 8 < sizeof(hr.content)) o += (size_t)snprintf(hr.content + o, sizeof(hr.content) - o, "\n\n%s", s.doc);
    if (s.path[0] && strcmp(s.kindname, "variable") && strcmp(s.kindname, "parameter") && o + 8 < sizeof(hr.content))
        snprintf(hr.content + o, sizeof(hr.content) - o, "\n\n*%s - `%s`*", s.kindname, display_path(st, s.path));
    else if (o + 8 < sizeof(hr.content))
        snprintf(hr.content + o, sizeof(hr.content) - o, "\n\n*%s*", s.kindname);
    return hr;
}

/* ── definition ──────────────────────────────────────────────────────────── */

DefLocation lsp_definition(LspState *st, int line, int col) {
    DefLocation d = {0};
    int off = lq_offset(st->text, line, col);

    char inc[128];
    if (include_path_at(st->text, off, inc, sizeof(inc))) {
        ResolvedInclude ri;
        char *fd = dir_of(st->path);
        loader_resolve_include(inc, st->zora.src_dir, fd, &ri);
        free(fd);
        const char *file = ri.hy && module_exists(ri.hy) ? ri.hy : ri.hyi;
        if (file) { d.found = 1; snprintf(d.path, sizeof(d.path), "%s", file); }
        loader_free_resolved(&ri);
        return d;
    }

    Sym s;
    resolve_symbol(st, line, col, &s);
    if (!s.found || !s.path[0]) return d;
    d.found = 1;
    snprintf(d.path, sizeof(d.path), "%s", s.path);
    d.line = s.line; d.col = s.col; d.end_col = s.end_col;
    return d;
}

/* ── signature help ──────────────────────────────────────────────────────── */

static void build_sig(SignatureInfo *si, const char *name, Type ret, ASTNode **params, int pc, int skip,
                      const char *owner) {
    char r[96];
    lq_type_str(ret, r, sizeof(r));
    int o = snprintf(si->label, sizeof(si->label), "%s%s%s%s(", r, " ", owner ? owner : "", name);
    if (owner) {   /* "Class." prefix was glued in front of the name - keep the label readable */
        o = snprintf(si->label, sizeof(si->label), "%s %s.%s(", r, owner, name);
    }
    si->param_count = 0;
    for (int i = skip; i < pc && o + 64 < (int)sizeof(si->label) && si->param_count < SIG_MAX_PARAMS; i++) {
        VarDeclNode *p = (VarDeclNode *)params[i];
        if (!p) continue;
        char tn[96];
        lq_type_str(p->var_type, tn, sizeof(tn));
        if (si->param_count) o += snprintf(si->label + o, sizeof(si->label) - (size_t)o, ", ");
        si->param_start[si->param_count] = o;
        o += snprintf(si->label + o, sizeof(si->label) - (size_t)o, "%s %s%s", tn, p->var_name ? p->var_name : "_",
                      p->initializer ? " = ..." : "");
        si->param_end[si->param_count] = o;
        si->param_count++;
    }
    snprintf(si->label + o, sizeof(si->label) - (size_t)o, ")");
    si->found = 1;
}

SignatureInfo lsp_signature_help(LspState *st, int line, int col) {
    SignatureInfo si; memset(&si, 0, sizeof(si));
    const char *t = st->text;
    int off = lq_offset(t, line, col);
    if (lq_lex_state(t, off) != 0) return si;

    /* the innermost unclosed `(` before the cursor, and how many commas are after it */
    int depth = 0, commas = 0, open = -1;
    for (int i = off - 1; i >= 0; i--) {
        char c = t[i];
        if (c == ')' || c == ']') depth++;
        else if (c == '(' || c == '[') { if (depth == 0) { if (c == '(') open = i; break; } depth--; }
        else if (c == ',' && depth == 0) commas++;
        else if ((c == ';' || c == '{' || c == '}') && depth == 0) break;
    }
    if (open < 0) return si;

    int ne = open;
    while (ne > 0 && isspace((unsigned char)t[ne - 1])) ne--;
    int ns = ne;
    while (ns > 0 && lq_is_ident(t[ns - 1])) ns--;
    if (ns == ne) return si;
    char name[128];
    int nl = ne - ns < 127 ? ne - ns : 127;
    memcpy(name, t + ns, (size_t)nl); name[nl] = '\0';

    Scope sc;
    lq_scope_at(st, line, col, &sc);
    int skip = 0;

    if (ns > 0 && t[ns - 1] == '.') {
        int cs = lq_chain_start(t, ns - 1);
        Resolved r = lq_resolve_chain(st, &sc, t, cs, ns - 1);
        if (r.kind == RES_MODULE) {
            ModuleNode *m = lq_find_module(st, r.name, NULL);
            for (int f = 0; m && f < m->func_count; f++)
                if (m->funcs[f] && !strcmp(m->funcs[f]->name, name)) {
                    build_sig(&si, name, m->funcs[f]->return_type, m->funcs[f]->params, m->funcs[f]->param_count, 0, r.name);
                    break;
                }
        } else if (r.kind == RES_TYPE && r.type.kind == TYPE_SIMPLE && r.type.name) {
            ClassNode *c = lq_find_class(st, r.type.name, NULL);
            MethodNode *m = c ? lq_find_method(c, name) : NULL;
            if (m) build_sig(&si, name, m->return_type, m->params, m->param_count, 0, c->name);
            else {
                FuncNode *f = lq_find_func(st, name, NULL);   /* x.f(...) is f(x, ...) */
                if (f) build_sig(&si, name, f->return_type, f->params, f->param_count, 1, NULL);
            }
        }
    } else {
        /* `new Class(` */
        char w[128];
        int e = ns;
        while (e > 0 && isspace((unsigned char)t[e - 1])) e--;
        int s2 = e;
        while (s2 > 0 && lq_is_ident(t[s2 - 1])) s2--;
        memcpy(w, t + s2, (size_t)(e - s2 < 127 ? e - s2 : 127)); w[e - s2 < 127 ? e - s2 : 127] = '\0';
        ClassNode *c = !strcmp(w, "new") ? lq_find_class(st, name, NULL) : NULL;
        if (c && c->has_ctor) {
            Type vt = { TYPE_SIMPLE, 0, c->name, NULL, 0, 0, 0 };
            build_sig(&si, name, vt, c->ctor_params, c->ctor_param_count, 0, NULL);
        } else {
            FuncNode *f = lq_find_func(st, name, NULL);
            if (f) build_sig(&si, name, f->return_type, f->params, f->param_count, 0, NULL);
            else if (sc.cls) {
                MethodNode *m = lq_find_method(sc.cls, name);
                if (m) build_sig(&si, name, m->return_type, m->params, m->param_count, 0, sc.cls->name);
            }
        }
    }
    (void)skip;
    if (!si.found) return si;
    si.active = commas < si.param_count ? commas : (si.param_count > 0 ? si.param_count - 1 : 0);
    return si;
}

/* ── document symbols ────────────────────────────────────────────────────── */

typedef struct { SymbolInfo *v; int n, cap; } SymList;

static int sym_push(SymList *l, const char *name, const char *detail, int kind, const ASTNode *node,
                    const char *text, int parent) {
    if (l->n == l->cap) { l->cap = l->cap ? l->cap * 2 : 64; l->v = realloc(l->v, (size_t)l->cap * sizeof(SymbolInfo)); }
    SymbolInfo *s = &l->v[l->n];
    memset(s, 0, sizeof(*s));
    snprintf(s->name, sizeof(s->name), "%s", name);
    snprintf(s->detail, sizeof(s->detail), "%s", detail ? detail : "");
    s->kind = kind;
    s->line = node->line > 0 ? node->line - 1 : 0;
    s->col = node->col;
    s->end_line = node->end_line > 0 ? node->end_line - 1 : s->line;
    s->end_col = node->end_col;
    s->name_col = node->col;
    s->parent = parent;
    int ls, le;
    if (lq_line_bounds(text, s->line, &ls, &le)) {
        size_t nl = strlen(name);
        for (int i = node->col > 0 ? node->col : 0; i + (int)nl <= le - ls; i++)
            if (!strncmp(text + ls + i, name, nl) && !lq_is_ident(text[ls + i + nl]) &&
                (i == 0 || !lq_is_ident(text[ls + i - 1]))) { s->name_col = i; break; }
    }
    /* the name must lie inside the full range */
    if (s->end_line < s->line) { s->end_line = s->line; s->end_col = s->name_col + (int)strlen(name); }
    return l->n++;
}

SymbolInfo *lsp_document_symbols(LspState *st, int *count) {
    *count = 0;
    if (!st->prog) return NULL;
    SymList l = {0};
    const char *t = st->text;
    char det[160];
    for (int d = 0; d < st->prog->decl_count; d++) {
        ASTNode *n = st->prog->declarations[d];
        if (!n) continue;
        switch (n->type) {
        case NODE_FUNC: {
            FuncNode *f = (FuncNode *)n;
            lq_func_sig(f->name, f->return_type, f->params, f->param_count, det, sizeof(det));
            sym_push(&l, f->name, det, 12, n, t, -1);
            break;
        }
        case NODE_CLASS: {
            ClassNode *c = (ClassNode *)n;
            int p = sym_push(&l, c->name, c->has_ctor ? "class" : "struct", c->has_ctor ? 5 : 23, n, t, -1);
            for (int i = 0; i < c->field_count; i++) {
                FieldNode *f = c->fields[i];
                if (!f || f->base.line <= 0) continue;
                char tn[96]; lq_type_str(f->field_type, tn, sizeof(tn));
                sym_push(&l, f->name, tn, 8, &f->base, t, p);
            }
            for (int i = 0; i < c->method_count; i++) {
                MethodNode *m = c->methods[i];
                if (!m || m->base.line <= 0) continue;
                lq_func_sig(m->name, m->return_type, m->params, m->param_count, det, sizeof(det));
                sym_push(&l, m->name, det, 6, &m->base, t, p);
            }
            break;
        }
        case NODE_ENUM: {
            EnumNode *e = (EnumNode *)n;
            int p = sym_push(&l, e->name, "enum", 10, n, t, -1);
            for (int i = 0; i < e->variant_count; i++) {
                snprintf(det, sizeof(det), "= %d", e->variants[i].value);
                sym_push(&l, e->variants[i].name, det, 22, n, t, p);
            }
            break;
        }
        case NODE_MODULE: {
            ModuleNode *m = (ModuleNode *)n;
            int p = sym_push(&l, m->name, "module", 2, n, t, -1);
            for (int i = 0; i < m->func_count; i++) {
                FuncNode *f = m->funcs[i];
                if (!f) continue;
                lq_func_sig(f->name, f->return_type, f->params, f->param_count, det, sizeof(det));
                sym_push(&l, f->name, det, 12, &f->base, t, p);
            }
            break;
        }
        case NODE_STATIC_VAR: {
            StaticVarNode *sv = (StaticVarNode *)n;
            char tn[96]; lq_type_str(sv->var_type, tn, sizeof(tn));
            sym_push(&l, sv->var_name, tn, sv->is_const ? 14 : 13, n, t, -1);
            break;
        }
        default: break;
        }
    }
    *count = l.n;
    return l.v;
}
