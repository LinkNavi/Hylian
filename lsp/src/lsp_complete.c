/* lsp_complete.c - context-aware completion.
 *
 * The cursor is classified first (comment, include path, member access, `new`,
 * type position, declaration name, or a plain identifier) and only then are
 * candidates gathered, so a field is offered after `p.` and nowhere else, and
 * nothing at all is offered where a new name is being introduced. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "lsp_internal.h"

/* ── output list with prefix filtering ───────────────────────────────────── */

typedef struct {
    CompletionItem *items;
    int  count, cap;
    char prefix[128];
    int  plen;
    int  truncated;
} Out;

#define MAX_ITEMS 1500

static int ci_prefix(const char *label, const char *prefix, int plen) {
    for (int i = 0; i < plen; i++) {
        if (!label[i]) return 0;
        if (tolower((unsigned char)label[i]) != tolower((unsigned char)prefix[i])) return 0;
    }
    return 1;
}

/* prefix match, or (for 3+ typed chars) the typed text appears after a word
   boundary: `Window` finds `InitWindow` / `close_window` */
static int matches(const Out *o, const char *label) {
    if (o->plen == 0) return 1;
    if (ci_prefix(label, o->prefix, o->plen)) return 1;
    if (o->plen >= 3) {
        for (const char *p = label + 1; *p; p++) {
            int boundary = p[-1] == '_' || p[-1] == '.' || (isupper((unsigned char)*p) && islower((unsigned char)p[-1]));
            if (boundary && ci_prefix(p, o->prefix, o->plen)) return 1;
        }
    }
    return 0;
}

static CompletionItem *out_add(Out *o, const char *label, CompletionKind kind, const char *sort_key) {
    if (!label || !label[0] || !matches(o, label)) return NULL;
    for (int i = 0; i < o->count; i++)
        if (!strcmp(o->items[i].label, label) && o->items[i].kind == kind) return NULL;
    if (o->count >= MAX_ITEMS) { o->truncated = 1; return NULL; }
    if (o->count == o->cap) {
        o->cap = o->cap ? o->cap * 2 : 128;
        o->items = realloc(o->items, (size_t)o->cap * sizeof(CompletionItem));
    }
    CompletionItem *it = &o->items[o->count++];
    memset(it, 0, sizeof(*it));
    snprintf(it->label, sizeof(it->label), "%s", label);
    it->kind = kind;
    snprintf(it->sort_key, sizeof(it->sort_key), "%s", sort_key);
    return it;
}

static void set_detail(CompletionItem *it, const char *d) {
    if (it) snprintf(it->detail, sizeof(it->detail), "%s", d);
}

/* `name(${1:a}, ${2:b})` for a call, starting at parameter `skip` */
static void set_call_snippet(CompletionItem *it, const char *name, ASTNode **params, int pc, int skip) {
    if (!it) return;
    char *b = it->insert_text;
    size_t n = sizeof(it->insert_text), o = (size_t)snprintf(b, n, "%s(", name);
    int shown = 0;
    for (int i = skip; i < pc && o + 16 < n; i++) {
        VarDeclNode *p = (VarDeclNode *)params[i];
        if (p && p->initializer) continue;   /* has a default: leave it out */
        o += (size_t)snprintf(b + o, n - o, "%s${%d:%s}", shown ? ", " : "", shown + 1,
                              p && p->var_name ? p->var_name : "arg");
        shown++;
    }
    snprintf(b + o, n - o, shown ? ")$0" : ")$0");
    it->is_snippet = 1;
}

/* ── the include block ───────────────────────────────────────────────────── */

/* is the cursor inside `include { ... }`? */
static int in_include(const char *t, int off) {
    for (int i = off - 1; i >= 0; i--) {
        if (t[i] == '}' || t[i] == ';') return 0;
        if (t[i] == '{') {
            int j = i - 1;
            while (j >= 0 && isspace((unsigned char)t[j])) j--;
            return j >= 6 && !strncmp(t + j - 6, "include", 7) && (j < 7 || !lq_is_ident(t[j - 7]));
        }
    }
    return 0;
}

static int already_included(LspState *st, const char *mod) {
    if (!st->prog) return 0;
    for (int i = 0; i < st->prog->include_count; i++)
        if (!strcmp(st->prog->includes[i], mod)) return 1;
    return 0;
}

static void complete_include(LspState *st, Out *o) {
    char names[512][96];
    char pending[512];
    int n;

    /* the stdlib root depends on the project (a checkout has its own) */
    char root[1024];
    std_root_for(&st->zora, root, sizeof(root));
    extern int std_module_names_at(const char *root, char names[][96], int max);
    n = std_module_names_at(root, names, 512);
    for (int i = 0; i < n; i++) {
        char full[128];
        snprintf(full, sizeof(full), "std.%s", names[i]);
        if (already_included(st, full)) continue;
        CompletionItem *it = out_add(o, full, COMPLETE_MODULE, "1");
        set_detail(it, "standard library");
    }
    n = vendor_module_names(&st->zora, names, pending, 512);
    for (int i = 0; i < n; i++) {
        char full[128];
        snprintf(full, sizeof(full), "vendors.%s", names[i]);
        if (already_included(st, full)) continue;
        CompletionItem *it = out_add(o, full, COMPLETE_MODULE, "0");
        set_detail(it, pending[i] ? "declared in Zora.toml - run `zora build` to generate bindings" : "vendor bindings");
    }
    n = project_module_names(&st->zora, st->path, names, 512);
    for (int i = 0; i < n; i++) {
        if (already_included(st, names[i])) continue;
        CompletionItem *it = out_add(o, names[i], COMPLETE_MODULE, "0");
        set_detail(it, "project module");
    }
}

/* ── member completion ───────────────────────────────────────────────────── */

typedef struct { Out *o; const char *type_name; LspState *st; } UfcsCtx;

static void ufcs_cb(ASTNode *d, Module *from, void *u) {
    UfcsCtx *c = u;
    if (d->type != NODE_FUNC) return;
    FuncNode *f = (FuncNode *)d;
    if (f->param_count < 1 || strstr(f->name, "__")) return;
    VarDeclNode *p0 = (VarDeclNode *)f->params[0];
    if (!p0 || p0->var_type.kind != TYPE_SIMPLE || !p0->var_type.name) return;
    if (strcmp(p0->var_type.name, c->type_name)) return;
    char sig[256];
    lq_func_sig(f->name, f->return_type, f->params, f->param_count, sig, sizeof(sig));
    CompletionItem *it = out_add(c->o, f->name, COMPLETE_FUNCTION, "4");
    if (it) {
        set_detail(it, sig);
        set_call_snippet(it, f->name, f->params, f->param_count, 1);
        if (from && from->text) lq_doc_above(from->text, f->base.line - 1, it->documentation, sizeof(it->documentation));
    }
}

static void complete_members(LspState *st, Out *o, int line, int col, int dot) {
    const char *t = st->text;
    Scope sc;
    lq_scope_at(st, line, col, &sc);
    int cs = lq_chain_start(t, dot);
    if (cs >= dot) return;
    Resolved r = lq_resolve_chain(st, &sc, t, cs, dot);

    if (r.kind == RES_ENUM) {
        EnumNode *en = lq_find_enum(st, r.name, NULL);
        for (int i = 0; en && i < en->variant_count; i++) {
            char det[128];
            snprintf(det, sizeof(det), "%s.%s = %d", en->name, en->variants[i].name, en->variants[i].value);
            set_detail(out_add(o, en->variants[i].name, COMPLETE_ENUM_MEMBER, "0"), det);
        }
        return;
    }
    if (r.kind == RES_MODULE) {
        ModuleNode *m = lq_find_module(st, r.name, NULL);
        for (int f = 0; m && f < m->func_count; f++) {
            FuncNode *fn = m->funcs[f];
            if (!fn || (m->func_is_public && !m->func_is_public[f])) continue;
            char sig[256];
            lq_func_sig(fn->name, fn->return_type, fn->params, fn->param_count, sig, sizeof(sig));
            CompletionItem *it = out_add(o, fn->name, COMPLETE_FUNCTION, "0");
            set_detail(it, sig);
            if (it) set_call_snippet(it, fn->name, fn->params, fn->param_count, 0);
        }
        return;
    }
    if (r.kind != RES_TYPE) return;   /* unknown receiver: offer nothing rather than everything */

    Type ty = r.type;
    if (ty.kind == TYPE_ARRAY) {
        char et[96] = "T";
        if (ty.elem_type_count > 0) lq_type_str(ty.elem_types[0], et, sizeof(et));
        set_detail(out_add(o, "len", COMPLETE_PROPERTY, "0"), "int - number of elements");
        set_detail(out_add(o, "cap", COMPLETE_PROPERTY, "0"), "int - allocated capacity");
        char d[160];
        snprintf(d, sizeof(d), "void push(%s value)", et);
        CompletionItem *it = out_add(o, "push", COMPLETE_METHOD, "0");
        set_detail(it, d); if (it) { snprintf(it->insert_text, sizeof(it->insert_text), "push(${1:value})$0"); it->is_snippet = 1; }
        snprintf(d, sizeof(d), "%s pop()", et);
        it = out_add(o, "pop", COMPLETE_METHOD, "0");
        set_detail(it, d); if (it) { snprintf(it->insert_text, sizeof(it->insert_text), "pop()"); it->is_snippet = 0; }
        return;
    }
    if (ty.kind != TYPE_SIMPLE || !ty.name) return;

    if (!strcmp(ty.name, "Error")) {
        CompletionItem *it = out_add(o, "message", COMPLETE_METHOD, "0");
        set_detail(it, "str message()"); if (it) snprintf(it->insert_text, sizeof(it->insert_text), "message()");
        it = out_add(o, "code", COMPLETE_METHOD, "0");
        set_detail(it, "int code()"); if (it) snprintf(it->insert_text, sizeof(it->insert_text), "code()");
    }

    ClassNode *cls = lq_find_class(st, ty.name, NULL);
    for (int i = 0; cls && i < cls->field_count; i++) {
        FieldNode *f = cls->fields[i];
        if (!f || !f->name) continue;
        if (!f->is_public && sc.cls != cls) continue;   /* private outside its class */
        char det[160], tn[96];
        lq_type_str(f->field_type, tn, sizeof(tn));
        snprintf(det, sizeof(det), "%s %s.%s", tn, cls->name, f->name);
        set_detail(out_add(o, f->name, COMPLETE_FIELD, "0"), det);
    }
    for (int i = 0; cls && i < cls->method_count; i++) {
        MethodNode *m = cls->methods[i];
        if (!m) continue;
        char sig[256];
        lq_func_sig(m->name, m->return_type, m->params, m->param_count, sig, sizeof(sig));
        CompletionItem *it = out_add(o, m->name, COMPLETE_METHOD, "0");
        set_detail(it, sig);
        if (it) set_call_snippet(it, m->name, m->params, m->param_count, 0);
    }

    /* `x.f()` also means f(x) (uniform call syntax): offer functions taking this type first */
    /* (only once something is typed: right after the dot, hundreds of C
       functions taking this struct first would bury its own members) */
    if (o->plen > 0) {
        UfcsCtx uc = { o, ty.name, st };
        lq_each_decl(st, ufcs_cb, &uc);
    }
}

/* ── plain identifiers ───────────────────────────────────────────────────── */

static const char *KEYWORDS[] = {
    "if", "else", "while", "for", "in", "return", "new", "nil", "true", "false",
    "break", "continue", "defer", "switch", "case", "default", "unsafe", "as", "cast",
    "sizeof", "addrof_fn", "do", NULL
};
static const char *DECL_KEYWORDS[] = {
    "class", "struct", "enum", "union", "module", "public", "private", "static", "const",
    "volatile", "packed", "naked", "include", "typedef", NULL
};
static const char *TYPES[] = {
    "int", "str", "bool", "void", "float", "float32", "float64", "Error", "usize", "isize",
    "int8", "int16", "int32", "int64", "uint8", "uint16", "uint32", "uint64", "array", "multi", "any", NULL
};

typedef struct { const char *name, *detail; } Builtin;
static const Builtin BUILTINS[] = {
    {"println", "void println(value)"}, {"print", "void print(value)"},
    {"panic", "void panic(str message)"}, {"Err", "Error Err(str message)"},
    {"len", "int len(array<T> arr)"}, {"push", "void push(array<T> arr, T value)"},
    {"pop", "T pop(array<T> arr)"}, {"exit", "void exit(int code)"},
    {"syscall", "int syscall(int nr, ...)"}, {"memset", "void memset(ptr, int value, int n)"},
    {"memcpy", "void memcpy(ptr dst, ptr src, int n)"}, {"size_of", "usize size_of(Type)"},
    {NULL, NULL}
};

typedef struct {
    Out *o;
    Scope *sc;
    LspState *st;
    int want;      /* 0 everything, 1 classes only (after new), 2 functions only (addrof_fn), 3 types only */
} IdentCtx;

static void ident_decl_cb(ASTNode *d, Module *from, void *u) {
    IdentCtx *c = u;
    Out *o = c->o;
    const char *own = from ? "3" : "2";
    char doc[512] = "";
    const char *text = from ? from->text : c->st->text;

    switch (d->type) {
    case NODE_FUNC: {
        if (c->want == 1 || c->want == 3) return;
        FuncNode *f = (FuncNode *)d;
        if (strstr(f->name, "__") || !matches(o, f->name)) return;
        char sig[256];
        lq_func_sig(f->name, f->return_type, f->params, f->param_count, sig, sizeof(sig));
        CompletionItem *it = out_add(o, f->name, COMPLETE_FUNCTION, own);
        if (!it) return;
        set_detail(it, sig);
        if (c->want != 2) set_call_snippet(it, f->name, f->params, f->param_count, 0);
        if (text) lq_doc_above(text, f->base.line - 1, doc, sizeof(doc));
        snprintf(it->documentation, sizeof(it->documentation), "%s", doc);
        break;
    }
    case NODE_CLASS: {
        if (c->want == 2) return;
        ClassNode *cn = (ClassNode *)d;
        CompletionItem *it = out_add(o, cn->name, cn->has_ctor ? COMPLETE_CLASS : COMPLETE_STRUCT, own);
        if (it) set_detail(it, cn->has_ctor ? "class" : "struct");
        break;
    }
    case NODE_ENUM: {
        if (c->want == 1 || c->want == 2) return;
        CompletionItem *it = out_add(o, ((EnumNode *)d)->name, COMPLETE_ENUM, own);
        if (it) set_detail(it, "enum");
        break;
    }
    case NODE_MODULE: {
        if (c->want != 0) return;
        CompletionItem *it = out_add(o, ((ModuleNode *)d)->name, COMPLETE_MODULE, own);
        if (it) set_detail(it, "module");
        break;
    }
    case NODE_STATIC_VAR: {
        if (c->want != 0) return;
        StaticVarNode *sv = (StaticVarNode *)d;
        char tn[96];
        lq_type_str(sv->var_type, tn, sizeof(tn));
        CompletionItem *it = out_add(o, sv->var_name, sv->is_const ? COMPLETE_CONSTANT : COMPLETE_VARIABLE, own);
        if (it) set_detail(it, tn);
        break;
    }
    default: break;
    }
}

/* ── auto-import: symbols from modules the file does not include yet ─────── */

static void include_edit(LspState *st, const char *mod, CompletionItem *it) {
    const char *t = st->text;
    const char *inc = strstr(t, "include");
    while (inc) {
        const char *p = inc + 7;
        while (*p && isspace((unsigned char)*p)) p++;
        if (*p == '{' && (inc == t || !lq_is_ident(inc[-1]))) {
            int line, col;
            lq_position(t, (int)(p - t) + 1, &line, &col);
            it->has_import = 1; it->import_line = line; it->import_col = col;
            snprintf(it->import_text, sizeof(it->import_text), "\n    %s,", mod);
            return;
        }
        inc = strstr(inc + 7, "include");
    }
    it->has_import = 1; it->import_line = 0; it->import_col = 0;
    snprintf(it->import_text, sizeof(it->import_text), "include { %s, }\n\n", mod);
}

static void auto_import_from(LspState *st, Out *o, Module *m, const char *modname) {
    if (!m || !m->prog) return;
    for (int d = 0; d < m->prog->decl_count; d++) {
        ASTNode *decl = m->prog->declarations[d];
        if (!decl) continue;
        const char *name = NULL; CompletionKind kind = COMPLETE_FUNCTION; char sig[256] = "";
        if (decl->type == NODE_FUNC) {
            FuncNode *f = (FuncNode *)decl;
            if (strstr(f->name, "__") || f->name[0] == '_') continue;
            name = f->name;
            lq_func_sig(f->name, f->return_type, f->params, f->param_count, sig, sizeof(sig));
        } else if (decl->type == NODE_CLASS) {
            name = ((ClassNode *)decl)->name; kind = COMPLETE_CLASS; snprintf(sig, sizeof(sig), "class");
        } else if (decl->type == NODE_ENUM) {
            name = ((EnumNode *)decl)->name; kind = COMPLETE_ENUM; snprintf(sig, sizeof(sig), "enum");
        } else if (decl->type == NODE_STATIC_VAR) {
            StaticVarNode *sv = (StaticVarNode *)decl;
            if (sv->var_name[0] == '_') continue;
            name = sv->var_name; kind = sv->is_const ? COMPLETE_CONSTANT : COMPLETE_VARIABLE;
            lq_type_str(sv->var_type, sig, sizeof(sig));
        }
        if (!name || !ci_prefix(name, o->prefix, o->plen)) continue;
        CompletionItem *it = out_add(o, name, kind, "9");
        if (!it) continue;
        char det[300];
        snprintf(det, sizeof(det), "%s  - auto-import %s", sig, modname);
        set_detail(it, det);
        include_edit(st, modname, it);
        if (decl->type == NODE_FUNC) {
            FuncNode *f = (FuncNode *)decl;
            set_call_snippet(it, f->name, f->params, f->param_count, 0);
        }
    }
}

static void complete_auto_imports(LspState *st, Out *o) {
    if (o->plen < 2) return;
    char names[256][96];
    char root[1024];
    std_root_for(&st->zora, root, sizeof(root));
    extern int std_module_names_at(const char *root, char names[][96], int max);
    int n = std_module_names_at(root, names, 256);
    for (int i = 0; i < n; i++) {
        char full[128], path[1400];
        snprintf(full, sizeof(full), "std.%s", names[i]);
        if (already_included(st, full)) continue;
        char rel[96]; snprintf(rel, sizeof(rel), "%s", names[i]);
        for (char *p = rel; *p; p++) if (*p == '.') *p = '/';
        snprintf(path, sizeof(path), "%s/%s.hy", root, rel);
        auto_import_from(st, o, module_get(st->proj, path, 0), full);
    }
    n = project_module_names(&st->zora, st->path, names, 256);
    for (int i = 0; i < n; i++) {
        if (already_included(st, names[i])) continue;
        char rel[96], path[1400];
        snprintf(rel, sizeof(rel), "%s", names[i]);
        for (char *p = rel; *p; p++) if (*p == '.') *p = '/';
        snprintf(path, sizeof(path), "%s/%s.hy", st->zora.src_dir, rel);
        auto_import_from(st, o, module_get(st->proj, path, 0), names[i]);
    }
}

static void complete_identifiers(LspState *st, Out *o, int line, int col, int want) {
    Scope sc;
    lq_scope_at(st, line, col, &sc);
    IdentCtx ic = { o, &sc, st, want };

    if (want == 0) {
        for (int i = sc.count - 1; i >= 0; i--) {
            char tn[96];
            lq_type_str(sc.vars[i].type, tn, sizeof(tn));
            set_detail(out_add(o, sc.vars[i].name, COMPLETE_VARIABLE, "0"), tn);
        }
        if (sc.cls) {
            for (int i = 0; i < sc.cls->field_count; i++) {
                FieldNode *f = sc.cls->fields[i];
                char tn[96];
                if (!f || !f->name) continue;
                lq_type_str(f->field_type, tn, sizeof(tn));
                set_detail(out_add(o, f->name, COMPLETE_FIELD, "1"), tn);
            }
            for (int i = 0; i < sc.cls->method_count; i++) {
                MethodNode *m = sc.cls->methods[i];
                char sig[256];
                if (!m) continue;
                lq_func_sig(m->name, m->return_type, m->params, m->param_count, sig, sizeof(sig));
                CompletionItem *it = out_add(o, m->name, COMPLETE_METHOD, "1");
                set_detail(it, sig);
                if (it) set_call_snippet(it, m->name, m->params, m->param_count, 0);
            }
        }
    }

    lq_each_decl(st, ident_decl_cb, &ic);

    if (want == 0 || want == 1 || want == 3) {
        if (want != 1) for (int i = 0; TYPES[i]; i++) out_add(o, TYPES[i], COMPLETE_KEYWORD, "5");
    }
    if (want == 0) {
        for (int i = 0; BUILTINS[i].name; i++) {
            CompletionItem *it = out_add(o, BUILTINS[i].name, COMPLETE_FUNCTION, "5");
            if (it) {
                set_detail(it, BUILTINS[i].detail);
                snprintf(it->insert_text, sizeof(it->insert_text), "%s($0)", BUILTINS[i].name);
                it->is_snippet = 1;
            }
        }
        for (int i = 0; KEYWORDS[i]; i++) {
            const char *kw = KEYWORDS[i];
            CompletionItem *it = out_add(o, kw, COMPLETE_KEYWORD, "5");
            if (!it) continue;
            if (!strcmp(kw, "if") || !strcmp(kw, "while")) {
                snprintf(it->insert_text, sizeof(it->insert_text), "%s (${1:condition}) {\n\t$0\n}", kw);
                it->is_snippet = 1; it->kind = COMPLETE_SNIPPET;
            } else if (!strcmp(kw, "for")) {
                snprintf(it->insert_text, sizeof(it->insert_text), "for (${1:i} in ${2:0}..${3:n}) {\n\t$0\n}");
                it->is_snippet = 1; it->kind = COMPLETE_SNIPPET;
            }
        }
        for (int i = 0; DECL_KEYWORDS[i]; i++) out_add(o, DECL_KEYWORDS[i], COMPLETE_KEYWORD, "6");
        complete_auto_imports(st, o);
    }
}

/* ── context detection ───────────────────────────────────────────────────── */

static int word_before(const char *t, int pos, char *out, size_t n, int *word_start) {
    int e = pos;
    while (e > 0 && isspace((unsigned char)t[e - 1])) e--;
    int s = e;
    while (s > 0 && lq_is_ident(t[s - 1])) s--;
    if (s == e) { out[0] = '\0'; if (word_start) *word_start = e; return 0; }
    size_t len = (size_t)(e - s) < n - 1 ? (size_t)(e - s) : n - 1;
    memcpy(out, t + s, len);
    out[len] = '\0';
    if (word_start) *word_start = s;
    return 1;
}

static int is_type_name(LspState *st, const char *w) {
    for (int i = 0; TYPES[i]; i++) if (!strcmp(TYPES[i], w)) return 1;
    return lq_find_class(st, w, NULL) || lq_find_enum(st, w, NULL);
}

/* `int |`  `MyType |`  `class |`: a NEW name is being written here - offering
   completions would only get in the way. */
static int naming_position(LspState *st, const char *t, int ps) {
    char w[128]; int ws;
    if (!word_before(t, ps, w, sizeof(w), &ws)) {
        /* `int[] |` / `Type? |` / `array<int> |` */
        int e = ps;
        while (e > 0 && isspace((unsigned char)t[e - 1])) e--;
        if (e > 0 && (t[e - 1] == '?' || t[e - 1] == '>' || t[e - 1] == ']')) {
            /* only when it really follows a type at a statement boundary */
            return 0;
        }
        return 0;
    }
    if (!strcmp(w, "class") || !strcmp(w, "struct") || !strcmp(w, "enum") || !strcmp(w, "union") ||
        !strcmp(w, "module") || !strcmp(w, "typedef") || !strcmp(w, "fn"))
        return 1;
    if (!is_type_name(st, w)) return 0;
    /* what precedes the type word must be a statement boundary */
    int e = ws;
    while (e > 0 && isspace((unsigned char)t[e - 1])) e--;
    if (e == 0) return 1;
    char p = t[e - 1];
    if (p == ';' || p == '{' || p == '}' || p == '(' || p == ',') return 1;
    char pw[128];
    if (word_before(t, ws, pw, sizeof(pw), NULL) &&
        (!strcmp(pw, "static") || !strcmp(pw, "public") || !strcmp(pw, "private") || !strcmp(pw, "const") ||
         !strcmp(pw, "volatile") || !strcmp(pw, "naked")))
        return 1;
    return 0;
}

CompletionItem *lsp_complete(LspState *st, int line, int col, int *count, int *incomplete) {
    *count = 0; *incomplete = 0;
    const char *t = st->text;
    int off = lq_offset(t, line, col);
    if (lq_lex_state(t, off) != 0) return NULL;      /* inside a comment or string */

    int ps = off;
    while (ps > 0 && lq_is_ident(t[ps - 1])) ps--;
    if (ps < off && isdigit((unsigned char)t[ps])) return NULL;   /* a number */

    Out o = {0};
    o.plen = off - ps < 127 ? off - ps : 127;
    memcpy(o.prefix, t + ps, (size_t)o.plen);
    o.prefix[o.plen] = '\0';

    if (in_include(t, off)) {
        /* dotted path: everything typed so far since the last separator counts */
        int ds = off;
        while (ds > 0 && (lq_is_ident(t[ds - 1]) || t[ds - 1] == '.')) ds--;
        o.plen = off - ds < 127 ? off - ds : 127;
        memcpy(o.prefix, t + ds, (size_t)o.plen);
        o.prefix[o.plen] = '\0';
        complete_include(st, &o);
    } else if (ps > 0 && t[ps - 1] == '.' && !(ps > 1 && t[ps - 2] == '.')) {
        complete_members(st, &o, line, col, ps - 1);
    } else if (ps > 0 && t[ps - 1] == '.') {
        return NULL;                                  /* range operator `..` */
    } else {
        char w[128];
        int want = 0;
        int have = word_before(t, ps, w, sizeof(w), NULL);
        if (have && !strcmp(w, "new")) want = 1;
        else if (ps > 0 && t[ps - 1] == '<') want = 3;          /* cast<|>, array<|> */
        else if (naming_position(st, t, ps)) return NULL;
        else {
            /* addrof_fn( | */
            int e = ps;
            while (e > 0 && isspace((unsigned char)t[e - 1])) e--;
            if (e > 0 && t[e - 1] == '(') {
                char w2[128];
                if (word_before(t, e - 1, w2, sizeof(w2), NULL) &&
                    (!strcmp(w2, "addrof_fn") || !strcmp(w2, "adrof_fn"))) want = 2;
            }
        }
        complete_identifiers(st, &o, line, col, want);
    }

    if (o.count == 0) { free(o.items); return NULL; }
    *count = o.count;
    *incomplete = 1;   /* results are filtered by what is typed, so ask again as it changes */
    return o.items;
}
