/* lsp_query.c - text position helpers, scope collection, symbol lookup and
 * expression-type resolution. Everything the editor features share. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "lsp_internal.h"

/* ── text ────────────────────────────────────────────────────────────────── */

int lq_is_ident(int c) { return isalnum((unsigned char)c) || c == '_'; }

int lq_offset(const char *text, int line, int col) {
    int off = 0;
    for (int l = 0; l < line; l++) {
        const char *nl = strchr(text + off, '\n');
        if (!nl) return (int)strlen(text);
        off = (int)(nl - text) + 1;
    }
    int len = 0;
    while (text[off + len] && text[off + len] != '\n') len++;
    return off + (col < len ? col : len);
}

void lq_position(const char *text, int offset, int *line, int *col) {
    int l = 0, ls = 0;
    for (int i = 0; i < offset && text[i]; i++)
        if (text[i] == '\n') { l++; ls = i + 1; }
    *line = l;
    *col = offset - ls;
}

int lq_line_bounds(const char *text, int line, int *start, int *end) {
    int off = 0;
    for (int l = 0; l < line; l++) {
        const char *nl = strchr(text + off, '\n');
        if (!nl) return 0;
        off = (int)(nl - text) + 1;
    }
    int e = off;
    while (text[e] && text[e] != '\n') e++;
    if (e > off && text[e - 1] == '\r') e--;
    *start = off; *end = e;
    return 1;
}

int lq_lex_state(const char *t, int off) {
    int st = 0, interp = 0;   /* 0 code, 1 line comment, 2 block comment, 3 string */
    for (int i = 0; i < off && t[i]; i++) {
        char c = t[i];
        switch (st) {
        case 0:
            if (c == '/' && t[i + 1] == '/') { st = 1; i++; }
            else if (c == '/' && t[i + 1] == '*') { st = 2; i++; }
            else if (c == '"') { st = 3; interp = 0; }
            else if (c == '\'') {                       /* char literal: 'a' or '\n' */
                int j = i + 1;
                if (t[j] == '\\') j++;
                if (t[j] && t[j + 1] == '\'') i = j + 1;
            }
            break;
        case 1: if (c == '\n') st = 0; break;
        case 2: if (c == '*' && t[i + 1] == '/') { st = 0; i++; } break;
        case 3:
            if (c == '\\') i++;
            else if (interp) { if (c == '}' && t[i + 1] == '}') { interp = 0; i++; } }
            else if (c == '"') st = 0;
            else if (c == '{' && t[i + 1] == '{') { interp = 1; i++; }
            break;
        }
    }
    if (st == 1 || st == 2) return 1;
    if (st == 3 && !interp) return 2;
    return 0;
}

/* ── types ───────────────────────────────────────────────────────────────── */

void lq_type_str(Type t, char *buf, size_t n) {
    switch (t.kind) {
    case TYPE_SIMPLE:
        snprintf(buf, n, "%s%s", t.name ? t.name : "?", t.nullable ? "?" : "");
        break;
    case TYPE_ARRAY: {
        char e[96] = "?";
        if (t.elem_type_count > 0) lq_type_str(t.elem_types[0], e, sizeof(e));
        if (t.fixed_size > 0) snprintf(buf, n, "array<%s, %d>", e, t.fixed_size);
        else                  snprintf(buf, n, "array<%s>", e);
        break;
    }
    case TYPE_MULTI:
        snprintf(buf, n, t.is_any ? "multi<any>" : "multi<...>");
        break;
    case TYPE_RAWPTR: case TYPE_REF: {
        char e[96] = "?";
        if (t.elem_type_count > 0) lq_type_str(t.elem_types[0], e, sizeof(e));
        snprintf(buf, n, "%c%s", t.kind == TYPE_RAWPTR ? '*' : '&', e);
        break;
    }
    default: snprintf(buf, n, "?");
    }
}

void lq_func_sig(const char *name, Type ret, ASTNode **params, int pc, char *buf, size_t n) {
    char r[96];
    lq_type_str(ret, r, sizeof(r));
    size_t o = (size_t)snprintf(buf, n, "%s %s(", r, name);
    for (int i = 0; i < pc && o < n; i++) {
        VarDeclNode *p = (VarDeclNode *)params[i];
        if (!p) continue;
        char t[96];
        lq_type_str(p->var_type, t, sizeof(t));
        o += (size_t)snprintf(buf + o, n - o, "%s%s %s%s", i ? ", " : "", t,
                              p->var_name ? p->var_name : "_", p->initializer ? " = ..." : "");
    }
    if (o < n) snprintf(buf + o, n - o, ")");
}

void lq_doc_above(const char *text, int line, char *out, size_t n) {
    out[0] = '\0';
    if (!text || line <= 0) return;
    int first = line;
    for (int l = line - 1; l >= 0; l--) {
        int s, e;
        if (!lq_line_bounds(text, l, &s, &e)) break;
        while (s < e && (text[s] == ' ' || text[s] == '\t')) s++;
        if (e - s >= 2 && text[s] == '/' && text[s + 1] == '/') first = l; else break;
    }
    size_t o = 0;
    for (int l = first; l < line && o + 2 < n; l++) {
        int s, e;
        lq_line_bounds(text, l, &s, &e);
        while (s < e && (text[s] == ' ' || text[s] == '\t')) s++;
        s += 2;
        if (s < e && text[s] == ' ') s++;
        int len = e - s;
        if (len > (int)(n - o - 2)) len = (int)(n - o - 2);
        memcpy(out + o, text + s, (size_t)len);
        o += (size_t)len;
        out[o++] = '\n';
    }
    while (o > 0 && out[o - 1] == '\n') o--;
    out[o] = '\0';
}

/* ── declarations the file can see ───────────────────────────────────────── */

void lq_each_decl(LspState *st, DeclFn fn, void *user) {
    for (int i = 0; i < st->deps.count; i++) {
        Module *m = st->deps.mods[i];
        if (!m->prog) continue;
        for (int d = 0; d < m->prog->decl_count; d++)
            if (m->prog->declarations[d]) fn(m->prog->declarations[d], m, user);
    }
    if (st->prog)
        for (int d = 0; d < st->prog->decl_count; d++)
            if (st->prog->declarations[d]) fn(st->prog->declarations[d], NULL, user);
}

typedef struct { const char *name; ASTNode *found; Module *from; int type; } FindCtx;

static void find_cb(ASTNode *d, Module *from, void *u) {
    FindCtx *c = u;
    if (c->found && !from) {
        /* the file's own declaration wins over an included one */
    } else if (c->found) return;
    const char *nm = NULL;
    if (d->type != c->type) return;
    switch (d->type) {
    case NODE_FUNC:       nm = ((FuncNode *)d)->name; break;
    case NODE_CLASS:      nm = ((ClassNode *)d)->name; break;
    case NODE_ENUM:       nm = ((EnumNode *)d)->name; break;
    case NODE_MODULE:     nm = ((ModuleNode *)d)->name; break;
    case NODE_STATIC_VAR: nm = ((StaticVarNode *)d)->var_name; break;
    default: return;
    }
    if (nm && !strcmp(nm, c->name)) { c->found = d; c->from = from; }
}

static ASTNode *find_decl(LspState *st, const char *name, int type, Module **from) {
    FindCtx c = { name, NULL, NULL, type };
    lq_each_decl(st, find_cb, &c);
    if (from) *from = c.from;
    return c.found;
}

FuncNode *lq_find_func(LspState *st, const char *name, Module **from) {
    return (FuncNode *)find_decl(st, name, NODE_FUNC, from);
}
ClassNode *lq_find_class(LspState *st, const char *name, Module **from) {
    return (ClassNode *)find_decl(st, name, NODE_CLASS, from);
}
EnumNode *lq_find_enum(LspState *st, const char *name, Module **from) {
    return (EnumNode *)find_decl(st, name, NODE_ENUM, from);
}
ModuleNode *lq_find_module(LspState *st, const char *name, Module **from) {
    return (ModuleNode *)find_decl(st, name, NODE_MODULE, from);
}
StaticVarNode *lq_find_static(LspState *st, const char *name, Module **from) {
    return (StaticVarNode *)find_decl(st, name, NODE_STATIC_VAR, from);
}

MethodNode *lq_find_method(ClassNode *cls, const char *name) {
    for (int i = 0; cls && i < cls->method_count; i++)
        if (cls->methods[i] && !strcmp(cls->methods[i]->name, name)) return cls->methods[i];
    return NULL;
}

FieldNode *lq_find_field(ClassNode *cls, const char *name) {
    for (int i = 0; cls && i < cls->field_count; i++)
        if (cls->fields[i] && cls->fields[i]->name && !strcmp(cls->fields[i]->name, name)) return cls->fields[i];
    return NULL;
}

/* ── scope at the cursor ─────────────────────────────────────────────────── */

/* positions compare as (line, col) with 1-based lines like the AST */
static int pos_lt(int l1, int c1, int l2, int c2) { return l1 < l2 || (l1 == l2 && c1 < c2); }

static int node_contains(const ASTNode *n, int line, int col) {
    if (!n || n->line <= 0) return 0;
    return !pos_lt(line, col, n->line, n->col) && !pos_lt(n->end_line, n->end_col, line, col);
}

static int node_ends_before(const ASTNode *n, int line, int col) {
    return n && n->line > 0 && !pos_lt(line, col, n->end_line, n->end_col);
}

static void scope_add(Scope *sc, const char *name, Type t, const ASTNode *at, int is_param) {
    if (!name || sc->count >= MAX_SCOPE) return;
    ScopeVar *v = &sc->vars[sc->count++];
    v->name = name; v->type = t; v->is_param = is_param;
    v->line = at ? at->line : 0; v->col = at ? at->col : 0;
}

static void scope_stmts(Scope *sc, ASTNode **stmts, int count, int line, int col);

static void scope_stmt(Scope *sc, ASTNode *s, int line, int col) {
    if (!s) return;
    int before = node_ends_before(s, line, col);
    int inside = node_contains(s, line, col);

    if (before && !inside) {
        Type t;
        if (s->type == NODE_VAR_DECL) {
            VarDeclNode *v = (VarDeclNode *)s;
            t = v->var_type;
            /* `x := expr` / auto: the type is whatever the initializer became */
            if (t.kind == TYPE_SIMPLE && t.name && !strcmp(t.name, "auto") && v->initializer)
                t = v->initializer->resolved_type;
            scope_add(sc, v->var_name, t, s, 0);
        } else if (s->type == NODE_STATIC_VAR) {
            StaticVarNode *v = (StaticVarNode *)s;
            scope_add(sc, v->var_name, v->var_type, s, 0);
        }
        return;
    }
    if (!inside) return;

    /* the cursor is on the declaration itself, before its initializer: the name is
       being pointed at, so it is in scope (hover, go-to-definition) */
    if (s->type == NODE_VAR_DECL) {
        VarDeclNode *v = (VarDeclNode *)s;
        int before_init = !v->initializer || v->initializer->line <= 0 ||
                          pos_lt(line, col, v->initializer->line, v->initializer->col);
        if (before_init) {
            Type t = v->var_type;
            if (t.kind == TYPE_SIMPLE && t.name && !strcmp(t.name, "auto") && v->initializer)
                t = v->initializer->resolved_type;
            scope_add(sc, v->var_name, t, s, 0);
        }
        return;
    }

    switch (s->type) {
    case NODE_IF: {
        IfNode *n = (IfNode *)s;
        scope_stmts(sc, n->then_body, n->then_count, line, col);
        scope_stmts(sc, n->else_body, n->else_count, line, col);
        break;
    }
    case NODE_WHILE: scope_stmts(sc, ((WhileNode *)s)->body, ((WhileNode *)s)->body_count, line, col); break;
    case NODE_FOR: {
        ForNode *n = (ForNode *)s;
        if (n->init && n->init->type == NODE_VAR_DECL) {
            VarDeclNode *v = (VarDeclNode *)n->init;
            scope_add(sc, v->var_name, v->var_type, n->init, 0);
        }
        scope_stmts(sc, n->body, n->body_count, line, col);
        break;
    }
    case NODE_FOR_IN: {
        ForInNode *n = (ForInNode *)s;
        Type t = { TYPE_SIMPLE, 0, NULL, NULL, 0, 0, 0 };
        if (n->collection) {
            Type ct = n->collection->resolved_type;
            if (ct.kind == TYPE_ARRAY && ct.elem_type_count > 0) t = ct.elem_types[0];
        }
        scope_add(sc, n->var_name, t, s, 0);
        scope_stmts(sc, n->body, n->body_count, line, col);
        break;
    }
    case NODE_UNSAFE: scope_stmts(sc, ((UnsafeBlockNode *)s)->body, ((UnsafeBlockNode *)s)->body_count, line, col); break;
    case NODE_DEFER:  scope_stmts(sc, ((DeferNode *)s)->body, ((DeferNode *)s)->body_count, line, col); break;
    case NODE_SWITCH: {
        SwitchNode *n = (SwitchNode *)s;
        for (int i = 0; i < n->case_count; i++)
            scope_stmts(sc, n->cases[i]->body, n->cases[i]->body_count, line, col);
        break;
    }
    default: break;
    }
}

static void scope_stmts(Scope *sc, ASTNode **stmts, int count, int line, int col) {
    for (int i = 0; i < count; i++) scope_stmt(sc, stmts[i], line, col);
}

static void scope_params(Scope *sc, ASTNode **params, int pc) {
    for (int i = 0; i < pc; i++) {
        VarDeclNode *p = (VarDeclNode *)params[i];
        if (p) scope_add(sc, p->var_name, p->var_type, &p->base, 1);
    }
}

void lq_scope_at(LspState *st, int line0, int col, Scope *out) {
    memset(out, 0, sizeof(*out));
    if (!st->prog) return;
    int line = line0 + 1;
    for (int d = 0; d < st->prog->decl_count; d++) {
        ASTNode *decl = st->prog->declarations[d];
        if (!decl || !node_contains(decl, line, col)) continue;
        if (decl->type == NODE_FUNC) {
            FuncNode *f = (FuncNode *)decl;
            out->func = f;
            scope_params(out, f->params, f->param_count);
            scope_stmts(out, f->body, f->body_count, line, col);
        } else if (decl->type == NODE_CLASS) {
            ClassNode *c = (ClassNode *)decl;
            out->cls = c;
            for (int m = 0; m < c->method_count; m++) {
                MethodNode *mn = c->methods[m];
                if (mn && node_contains(&mn->base, line, col)) {
                    out->method = mn;
                    scope_params(out, mn->params, mn->param_count);
                    scope_stmts(out, mn->body, mn->body_count, line, col);
                }
            }
            if (c->has_ctor && c->ctor_params) {
                scope_params(out, c->ctor_params, c->ctor_param_count);
                scope_stmts(out, c->ctor_body, c->ctor_body_count, line, col);
            }
        } else if (decl->type == NODE_MODULE) {
            ModuleNode *m = (ModuleNode *)decl;
            out->mod = m;
            for (int f = 0; f < m->func_count; f++)
                if (m->funcs[f] && node_contains(&m->funcs[f]->base, line, col)) {
                    out->func = m->funcs[f];
                    scope_params(out, m->funcs[f]->params, m->funcs[f]->param_count);
                    scope_stmts(out, m->funcs[f]->body, m->funcs[f]->body_count, line, col);
                }
        }
        break;
    }
}

/* ── expression chains: `a.b(1).c[0]` ────────────────────────────────────── */

static int skip_back_balanced(const char *t, int i) {   /* t[i] is ')' or ']'; returns index of the opener */
    char close = t[i], open = close == ')' ? '(' : '[';
    int depth = 0;
    for (; i >= 0; i--) {
        if (t[i] == close) depth++;
        else if (t[i] == open && --depth == 0) return i;
    }
    return 0;
}

int lq_chain_start(const char *t, int dot) {
    int i = dot;
    for (;;) {
        while (i > 0 && (t[i - 1] == ')' || t[i - 1] == ']')) i = skip_back_balanced(t, i - 1);
        while (i > 0 && lq_is_ident(t[i - 1])) i--;
        if (i > 0 && t[i - 1] == '.' && !(i > 1 && t[i - 2] == '.')) { i--; continue; }
        break;
    }
    return i;
}

static int skip_fwd_balanced(const char *t, int i, int end) {   /* t[i] is '(' or '['; returns index after the closer */
    char open = t[i], close = open == '(' ? ')' : ']';
    int depth = 0;
    for (; i < end; i++) {
        if (t[i] == '"') { for (i++; i < end && t[i] != '"'; i++) if (t[i] == '\\') i++; continue; }
        if (t[i] == open) depth++;
        else if (t[i] == close && --depth == 0) return i + 1;
    }
    return end;
}

static Type simple_type(const char *name) {
    Type t = { TYPE_SIMPLE, 0, NULL, NULL, 0, 0, 0 };
    t.name = (char *)name;
    return t;
}

static int type_is_unknown(Type t) {
    return t.kind == TYPE_SIMPLE && (!t.name || !strcmp(t.name, "auto") || !strcmp(t.name, "unknown"));
}

static Resolved res_type(Type t) {
    Resolved r; memset(&r, 0, sizeof(r));
    if (type_is_unknown(t)) return r;
    r.kind = RES_TYPE; r.type = t;
    if (t.kind == TYPE_SIMPLE && t.name) snprintf(r.name, sizeof(r.name), "%s", t.name);
    return r;
}

Resolved lq_resolve_chain(LspState *st, const Scope *sc, const char *t, int start, int end) {
    Resolved cur; memset(&cur, 0, sizeof(cur));
    int i = start, first = 1;
    while (i < end) {
        while (i < end && isspace((unsigned char)t[i])) i++;
        if (!first) {
            if (i >= end || t[i] != '.') return (Resolved){0};
            i++;
            while (i < end && isspace((unsigned char)t[i])) i++;
        }
        int ns = i;
        while (i < end && lq_is_ident(t[i])) i++;
        if (i == ns) return (Resolved){0};
        char name[128];
        int nl = i - ns < 127 ? i - ns : 127;
        memcpy(name, t + ns, (size_t)nl); name[nl] = '\0';

        /* cast<T>(x) yields T */
        Type cast_t = {0}; int is_cast = 0;
        if (first && !strcmp(name, "cast") && i < end && t[i] == '<') {
            int ts = i + 1, depth = 1, j = ts;
            while (j < end && depth > 0) { if (t[j] == '<') depth++; else if (t[j] == '>') depth--; j++; }
            char tn[128];
            int tl = (j - 1 - ts) < 127 ? (j - 1 - ts) : 127;
            if (tl > 0) {
                memcpy(tn, t + ts, (size_t)tl); tn[tl] = '\0';
                char *s = tn; while (*s == ' ') s++;
                Module *fm; (void)fm;
                cast_t = simple_type(strdup(s));
                is_cast = 1;
            }
            i = j;
        }

        int called = 0, indexes = 0;
        while (i < end && (t[i] == '(' || t[i] == '[')) {
            if (t[i] == '(') called = 1; else indexes++;
            i = skip_fwd_balanced(t, i, end);
        }

        Resolved next; memset(&next, 0, sizeof(next));
        if (first) {
            if (is_cast) next = res_type(cast_t);
            else if (called) {
                FuncNode *f = lq_find_func(st, name, NULL);
                if (f) next = res_type(f->return_type);
                else if (sc->cls) {
                    MethodNode *m = lq_find_method(sc->cls, name);
                    if (m) next = res_type(m->return_type);
                }
            } else {
                const ScopeVar *v = NULL;
                for (int k = sc->count - 1; k >= 0; k--)
                    if (!strcmp(sc->vars[k].name, name)) { v = &sc->vars[k]; break; }
                if (v) next = res_type(v->type);
                else if (!strcmp(name, "self") && sc->cls) next = res_type(simple_type(sc->cls->name));
                else if (sc->cls && lq_find_field(sc->cls, name)) next = res_type(lq_find_field(sc->cls, name)->field_type);
                else {
                    StaticVarNode *sv = lq_find_static(st, name, NULL);
                    if (sv) next = res_type(sv->var_type);
                    else if (lq_find_enum(st, name, NULL)) { next.kind = RES_ENUM; snprintf(next.name, sizeof(next.name), "%s", name); }
                    else if (lq_find_module(st, name, NULL)) { next.kind = RES_MODULE; snprintf(next.name, sizeof(next.name), "%s", name); }
                }
            }
        } else if (cur.kind == RES_MODULE) {
            ModuleNode *m = lq_find_module(st, cur.name, NULL);
            for (int f = 0; m && f < m->func_count; f++)
                if (m->funcs[f] && !strcmp(m->funcs[f]->name, name)) { next = res_type(m->funcs[f]->return_type); break; }
        } else if (cur.kind == RES_ENUM) {
            next = res_type(simple_type("int"));
        } else if (cur.kind == RES_TYPE) {
            Type ct = cur.type;
            if (ct.kind == TYPE_ARRAY) {
                if (!strcmp(name, "len") || !strcmp(name, "cap")) next = res_type(simple_type("int"));
                else if (!strcmp(name, "pop") && ct.elem_type_count > 0) next = res_type(ct.elem_types[0]);
            } else if (ct.kind == TYPE_SIMPLE && ct.name) {
                ClassNode *c = lq_find_class(st, ct.name, NULL);
                if (c) {
                    if (called) { MethodNode *m = lq_find_method(c, name); if (m) next = res_type(m->return_type); }
                    else        { FieldNode *f = lq_find_field(c, name);   if (f) next = res_type(f->field_type); }
                }
                if (next.kind == RES_NONE && called) {
                    /* UFCS: `x.f()` calls f(x) */
                    FuncNode *f = lq_find_func(st, name, NULL);
                    if (f) next = res_type(f->return_type);
                }
            }
        }

        while (indexes-- > 0 && next.kind == RES_TYPE) {
            Type et = next.type;
            if (et.kind == TYPE_ARRAY && et.elem_type_count > 0) next = res_type(et.elem_types[0]);
            else if (et.kind == TYPE_RAWPTR && et.elem_type_count > 0) next = res_type(et.elem_types[0]);
            else if (et.kind == TYPE_SIMPLE && et.name && !strcmp(et.name, "str")) next = res_type(simple_type("uint8"));
            else next = (Resolved){0};
        }
        cur = next;
        if (cur.kind == RES_NONE) return cur;
        first = 0;
    }
    return cur;
}
