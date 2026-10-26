/* lsp_modules.c - the module cache and include resolution.
 *
 * A "module" is one parsed file: a .hy source or a .hyi interface. Includes are
 * resolved with the compiler's own resolver (loader.c), so `std.*`, `vendors.*`
 * and project modules land on exactly the files a real build would use. What
 * the editor has open but unsaved shadows the disk copy, so cross-file
 * completion sees edits as they are typed. */

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include "lsp_internal.h"
#include "diag.h"
#include "lsp_log.h"

/* flex's string-buffer entry points (the generated scanner does not export a header) */
typedef struct yy_buffer_state *YY_BUFFER_STATE;
extern YY_BUFFER_STATE yy_scan_string(const char *str);
extern void yy_delete_buffer(YY_BUFFER_STATE b);
extern int yyparse(void);
extern int yylineno;
extern FILE *yyin;
extern ProgramNode *root;
extern const char *current_parse_file;
extern int lex_col;
extern int g_loc_l1, g_loc_c1, g_loc_l2, g_loc_c2;

/* ── diagnostics capture ─────────────────────────────────────────────────── */

typedef struct {
    DiagSeverity sev;
    int  line, col, span;      /* line 1-based, col 0-based (-1 unknown) */
    char message[512];
    char hint[256];
} RawDiag;

#define RAW_MAX 128
static RawDiag g_raw[RAW_MAX];
static int     g_raw_count = 0;
static int     g_capture = 0;   /* 0 = discard, 1 = record */

static void capture_sink(DiagSeverity sev, const char *file, int line, int col,
                         const char *message, const char *hint, void *user) {
    (void)file; (void)user;
    if (!g_capture || g_raw_count >= RAW_MAX) return;
    RawDiag *d = &g_raw[g_raw_count++];
    d->sev = sev; d->line = line; d->col = col; d->span = diag_span_len();
    snprintf(d->message, sizeof(d->message), "%s", message);
    snprintf(d->hint, sizeof(d->hint), "%s", hint ? hint : "");
    diag_strip_ansi(d->hint);
}

void lsp_capture_begin(void) { g_raw_count = 0; g_capture = 1; diag_set_sink(capture_sink, NULL); }
void lsp_capture_end(void)   { g_capture = 0; }
int  lsp_capture_count(void) { return g_raw_count; }

/* copy out for lsp_analysis.c */
int lsp_capture_get(int i, int *sev, int *line, int *col, int *span,
                    const char **message, const char **hint) {
    if (i < 0 || i >= g_raw_count) return 0;
    *sev = (int)g_raw[i].sev; *line = g_raw[i].line; *col = g_raw[i].col;
    *span = g_raw[i].span; *message = g_raw[i].message; *hint = g_raw[i].hint;
    return 1;
}

/* ── parsing ─────────────────────────────────────────────────────────────── */

ProgramNode *lsp_parse_text(const char *path, const char *text) {
    root = NULL;
    current_parse_file = path;
    yylineno = 1;
    lex_col = 0;
    g_loc_l1 = g_loc_c1 = g_loc_l2 = g_loc_c2 = 0;
    /* flex's input() (used to skip comments) calls yyrestart(yyin) when it hits
       the end of a string buffer; with yyin unset that leaves the scanner
       reading from NULL. An empty file makes the end of input clean. */
    static FILE *devnull = NULL;
    if (!devnull) devnull = fopen("/dev/null", "r");
    YY_BUFFER_STATE buf = yy_scan_string(text);
    yyin = devnull;     /* after the scan: switching buffers reloads yyin from the buffer (NULL) */
    yyparse();
    yy_delete_buffer(buf);
    ProgramNode *p = root;
    root = NULL;
    return p;
}

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    if (sz < 0) { fclose(f); return NULL; }
    char *buf = malloc((size_t)sz + 1);
    size_t got = fread(buf, 1, (size_t)sz, f);
    buf[got] = '\0';
    fclose(f);
    return buf;
}

int module_exists(const char *path) {
    struct stat st;
    return path && stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

/* ── overlay (unsaved editor buffers) ────────────────────────────────────── */

static Overlay *overlay_find(LspProject *p, const char *path) {
    for (int i = 0; i < p->overlay_count; i++)
        if (!strcmp(p->overlays[i].path, path)) return &p->overlays[i];
    return NULL;
}

void lsp_project_set_overlay(LspProject *p, const char *path, const char *text) {
    Overlay *o = overlay_find(p, path);
    if (!text) {
        if (!o) return;
        free(o->text);
        *o = p->overlays[--p->overlay_count];
        return;
    }
    if (!o) {
        if (p->overlay_count >= MAX_OVERLAYS) return;
        o = &p->overlays[p->overlay_count++];
        snprintf(o->path, sizeof(o->path), "%s", path);
        o->text = NULL;
    }
    if (o->text && !strcmp(o->text, text)) return;   /* unchanged: keep the version */
    free(o->text);
    o->text = strdup(text);
    o->version = ++p->next_version + 1000000000L;    /* never collides with an mtime */
}

/* ── module cache ────────────────────────────────────────────────────────── */

static Module *find_mod(LspProject *p, const char *path) {
    for (int i = 0; i < p->mod_count; i++)
        if (!strcmp(p->mods[i]->path, path)) return p->mods[i];
    return NULL;
}

Module *module_get(LspProject *p, const char *path, int is_hyi) {
    struct stat st;
    Overlay *ov = is_hyi ? NULL : overlay_find(p, path);
    if (!ov && stat(path, &st) != 0) return NULL;
    long stamp = ov ? ov->version : (long)st.st_mtime * 1000L + (st.st_mtim.tv_nsec / 1000000L);

    Module *m = find_mod(p, path);
    if (m && m->stamp == stamp) return m;

    if (!m) {
        if (p->mod_count == p->mod_cap) {
            p->mod_cap = p->mod_cap ? p->mod_cap * 2 : 64;
            p->mods = realloc(p->mods, (size_t)p->mod_cap * sizeof(Module *));
        }
        m = calloc(1, sizeof(Module));
        snprintf(m->path, sizeof(m->path), "%s", path);
        m->is_hyi = is_hyi;
        p->mods[p->mod_count++] = m;
    }
    m->stamp = stamp;
    free(m->text);
    m->text = NULL;
    m->prog = NULL;   /* the old AST is abandoned, not freed: nodes may still be referenced */

    int saved_capture = g_capture;         /* a dependency's own parse errors are not reported */
    g_capture = 0;
    diag_set_sink(capture_sink, NULL);
    if (is_hyi) {
        char *link = NULL; char **pkgs = NULL; int npk = 0;
        m->prog = parse_hyi_into_program(path, &link, &pkgs, &npk);
        m->text = read_file(path);   /* for doc comments and locating names */
        free(link);
        for (int i = 0; i < npk; i++) free(pkgs[i]);
        free(pkgs);
    } else {
        m->text = ov ? strdup(ov->text) : read_file(path);
        if (m->text) m->prog = lsp_parse_text(path, m->text);
    }
    g_capture = saved_capture;
    return m;
}

void lsp_project_file_changed(LspProject *p, const char *path) {
    Module *m = find_mod(p, path);
    if (m) m->stamp = -1;
    zora_invalidate();
}

/* ── stdlib roots ────────────────────────────────────────────────────────── */

static int dir_has_hy(const char *dir) {
    DIR *d = opendir(dir);
    if (!d) return 0;
    struct dirent *e;
    int found = 0;
    while ((e = readdir(d)) != NULL) {
        size_t n = strlen(e->d_name);
        if (n > 3 && !strcmp(e->d_name + n - 3, ".hy")) { found = 1; break; }
    }
    closedir(d);
    return found;
}

void std_root_for(const ZoraInfo *z, char *out, size_t n) {
    char cand[6][1200];
    int c = 0;
    const char *lib = getenv("HYLIAN_LIB");
    if (lib && *lib) {
        snprintf(cand[c++], sizeof(cand[0]), "%s/std", lib);
        snprintf(cand[c++], sizeof(cand[0]), "%s", lib);
    }
    snprintf(cand[c++], sizeof(cand[0]), "/usr/local/lib/hylian/std");
    const char *home = getenv("HOME");
    if (home) snprintf(cand[c++], sizeof(cand[0]), "%s/.hylian/std", home);
    /* a checkout of the Hylian repo itself */
    snprintf(cand[c++], sizeof(cand[0]), "%s/stdlib", z->workspace);
    snprintf(cand[c++], sizeof(cand[0]), "%s/runtime/std", z->root);
    for (int i = 0; i < c; i++)
        if (dir_has_hy(cand[i])) { snprintf(out, n, "%s", cand[i]); return; }
    out[0] = '\0';
}

/* ── include resolution for one file ─────────────────────────────────────── */

static void resolve_one(const ZoraInfo *z, const char *inc, const char *src_dir,
                        const char *file_dir, ResolvedInclude *ri) {
    /* `std.x` means the standard library. The compiler also tries src_dir itself
       as a std root (for building the stdlib), which in an editor would let a
       file named map.hy that includes std.map resolve to ITSELF - so look in the
       real stdlib first. */
    if (!strncmp(inc, "std.", 4)) {
        char root[1024];
        std_root_for(z, root, sizeof(root));
        if (root[0]) {
            char rel[256];
            snprintf(rel, sizeof(rel), "%s", inc + 4);
            for (char *p = rel; *p; p++) if (*p == '.') *p = '/';
            char hy[1400], hyi[1400];
            snprintf(hy, sizeof(hy), "%s/%s.hy", root, rel);
            snprintf(hyi, sizeof(hyi), "%s/%s.hyi", root, rel);
            int has_hy = module_exists(hy), has_hyi = module_exists(hyi);
            if (has_hy || has_hyi) {
                memset(ri, 0, sizeof(*ri));
                ri->kind = INC_STD;
                if (has_hy)  { ri->hy = strdup(hy); ri->hy_root = strdup(root); }
                if (has_hyi) ri->hyi = strdup(hyi);
                return;
            }
        }
    }
    loader_resolve_include(inc, src_dir, file_dir, ri);
    if (ri->kind == INC_STD && !ri->hy && !ri->hyi) {
        /* the compiler would look in $HYLIAN_LIB / the installed toolchain; an
           editor often runs without that environment, so also try where a
           checkout or user install keeps the stdlib */
        char root[1024];
        std_root_for(z, root, sizeof(root));
        if (root[0]) {
            char rel[256];
            snprintf(rel, sizeof(rel), "%s", inc + 4);
            for (char *p = rel; *p; p++) if (*p == '.') *p = '/';
            char path[1400];
            snprintf(path, sizeof(path), "%s/%s.hy", root, rel);
            if (module_exists(path)) { ri->hy = strdup(path); ri->hy_root = strdup(root); }
            snprintf(path, sizeof(path), "%s/%s.hyi", root, rel);
            if (module_exists(path)) ri->hyi = strdup(path);
        }
    }
}

static int dep_has(const DepSet *d, const char *path) {
    for (int i = 0; i < d->count; i++)
        if (!strcmp(d->mods[i]->path, path)) return 1;
    return 0;
}

static void dep_add(LspState *st, Module *m) {
    if (!m || st->deps.count >= MAX_DEPS || dep_has(&st->deps, m->path)) return;
    st->deps.mods[st->deps.count++] = m;
}

static void note_unresolved(LspState *st, const char *inc, const char *fmt, ...) {
    if (st->unresolved_count >= 16) return;
    snprintf(st->unresolved[st->unresolved_count].inc, 128, "%s", inc);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(st->unresolved[st->unresolved_count].reason, 400, fmt, ap);
    va_end(ap);
    st->unresolved_count++;
}

static void walk_includes(LspState *st, char **incs, int n, const char *src_dir,
                          const char *file_dir, int depth, int own) {
    if (depth > 12) return;
    for (int i = 0; i < n; i++) {
        const char *inc = incs[i];
        ResolvedInclude ri;
        resolve_one(&st->zora, inc, src_dir, file_dir, &ri);

        if (ri.kind == INC_STD) {
            if (ri.hyi) dep_add(st, module_get(st->proj, ri.hyi, 1));
            if (ri.hy) {
                if (!dep_has(&st->deps, ri.hy)) {
                    Module *m = module_get(st->proj, ri.hy, 0);
                    if (m) {
                        snprintf(m->std_root, sizeof(m->std_root), "%s", ri.hy_root ? ri.hy_root : "");
                        dep_add(st, m);
                        if (m->prog) {
                            char *fd = dir_of(m->path);
                            walk_includes(st, m->prog->includes, m->prog->include_count,
                                          ri.hy_root ? ri.hy_root : src_dir, fd, depth + 1, 0);
                            free(fd);
                        }
                    }
                }
            } else if (!ri.hyi && own) {
                char root[1024];
                std_root_for(&st->zora, root, sizeof(root));
                if (root[0]) note_unresolved(st, inc, "no module '%s' in the standard library (%s)", inc + 4, root);
                else note_unresolved(st, inc, "the Hylian standard library was not found "
                                              "(set HYLIAN_LIB or install it to /usr/local/lib/hylian/std)");
            }
        } else if (ri.kind == INC_VENDOR) {
            if (ri.hyi) dep_add(st, module_get(st->proj, ri.hyi, 1));
            else if (own) {
                const char *name = inc + 8;
                int declared = 0;
                for (int d = 0; d < st->zora.dep_count; d++)
                    if (!strcmp(st->zora.deps[d], name)) declared = 1;
                if (declared)
                    note_unresolved(st, inc, "no bindings for '%s' yet: run `zora build` to generate vendors/%s/%s.hyi",
                                    name, name, name);
                else
                    note_unresolved(st, inc, "no interface file vendors/%s/%s.hyi under the project root; declare it "
                                             "as a [[target.*.deps]] system dependency in Zora.toml", name, name);
            }
        } else if (ri.kind == INC_FILE) {
            if (ri.hy && module_exists(ri.hy)) {
                if (!dep_has(&st->deps, ri.hy) && strcmp(ri.hy, st->path) != 0) {
                    Module *m = module_get(st->proj, ri.hy, 0);
                    if (m) {
                        dep_add(st, m);
                        if (m->prog) {
                            char *fd = dir_of(m->path);
                            walk_includes(st, m->prog->includes, m->prog->include_count,
                                          src_dir, fd, depth + 1, 0);
                            free(fd);
                        }
                    }
                }
            } else if (own) {
                note_unresolved(st, inc, "no file for module '%s' under %s", inc, src_dir);
            }
        }
        loader_free_resolved(&ri);
    }
}

void deps_collect(LspState *st) {
    st->deps.count = 0;
    st->unresolved_count = 0;
    if (!st->prog) return;
    char *fd = dir_of(st->path);
    walk_includes(st, st->prog->includes, st->prog->include_count, st->zora.src_dir, fd, 0, 1);
    free(fd);
}

/* ── the typechecked program: own file + bodiless copies of everything it sees ── */

static FuncNode *strip_func(FuncNode *f) {
    FuncNode *c = malloc(sizeof(FuncNode));
    *c = *f;
    c->body = NULL; c->body_count = 0;
    c->base.from_include = 1;
    return c;
}

static ASTNode *strip_decl(ASTNode *d) {
    switch (d->type) {
    case NODE_FUNC:
        return (ASTNode *)strip_func((FuncNode *)d);
    case NODE_CLASS: {
        ClassNode *src = (ClassNode *)d, *c = malloc(sizeof(ClassNode));
        *c = *src;
        c->ctor_body = NULL; c->ctor_body_count = 0;
        if (src->method_count > 0) {
            c->methods = malloc((size_t)src->method_count * sizeof(MethodNode *));
            for (int i = 0; i < src->method_count; i++) {
                MethodNode *m = malloc(sizeof(MethodNode));
                *m = *src->methods[i];
                m->body = NULL; m->body_count = 0;
                c->methods[i] = m;
            }
        }
        c->base.from_include = 1;
        return (ASTNode *)c;
    }
    case NODE_MODULE: {
        ModuleNode *src = (ModuleNode *)d, *c = malloc(sizeof(ModuleNode));
        *c = *src;
        if (src->func_count > 0) {
            c->funcs = malloc((size_t)src->func_count * sizeof(FuncNode *));
            for (int i = 0; i < src->func_count; i++) c->funcs[i] = strip_func(src->funcs[i]);
        }
        c->base.from_include = 1;
        return (ASTNode *)c;
    }
    case NODE_STATIC_VAR: {
        StaticVarNode *src = (StaticVarNode *)d, *c = malloc(sizeof(StaticVarNode));
        *c = *src;
        int is_auto = src->var_type.name && !strcmp(src->var_type.name, "auto");
        if (!is_auto) c->initializer = NULL;
        c->base.from_include = 1;
        return (ASTNode *)c;
    }
    default:
        return d;
    }
}

ProgramNode *build_check_program(LspState *st) {
    ProgramNode *p = calloc(1, sizeof(ProgramNode));
    p->base = st->prog->base;
    int cap = st->prog->decl_count;
    for (int i = 0; i < st->deps.count; i++)
        if (st->deps.mods[i]->prog) cap += st->deps.mods[i]->prog->decl_count;
    p->declarations = malloc((size_t)(cap ? cap : 1) * sizeof(ASTNode *));
    for (int i = 0; i < st->deps.count; i++) {
        ProgramNode *dp = st->deps.mods[i]->prog;
        if (!dp) continue;
        for (int d = 0; d < dp->decl_count; d++)
            if (dp->declarations[d]) p->declarations[p->decl_count++] = strip_decl(dp->declarations[d]);
    }
    for (int d = 0; d < st->prog->decl_count; d++)
        p->declarations[p->decl_count++] = st->prog->declarations[d];
    p->includes = st->prog->includes;
    p->include_count = st->prog->include_count;
    return p;
}

/* ── name listings for include completion ────────────────────────────────── */

static int name_seen(char names[][96], int n, const char *s) {
    for (int i = 0; i < n; i++) if (!strcmp(names[i], s)) return 1;
    return 0;
}

static void scan_hy_tree(const char *root, const char *rel_dir, int depth, const char *skip_abs,
                         char names[][96], int *n, int max, int skip_platform) {
    if (depth > 6 || *n >= max) return;
    char dir[1500];
    snprintf(dir, sizeof(dir), "%s%s%s", root, rel_dir[0] ? "/" : "", rel_dir);
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && *n < max) {
        if (e->d_name[0] == '.') continue;
        char full[1800];
        snprintf(full, sizeof(full), "%s/%s", dir, e->d_name);
        struct stat st;
        if (stat(full, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            if (!strcmp(e->d_name, "vendors") || !strcmp(e->d_name, "zora-build") ||
                !strcmp(e->d_name, "node_modules") || !strcmp(e->d_name, "target")) continue;
            if (skip_platform && !strcmp(e->d_name, "platform")) continue;
            char sub[600];
            snprintf(sub, sizeof(sub), "%s%s%s", rel_dir, rel_dir[0] ? "/" : "", e->d_name);
            scan_hy_tree(root, sub, depth + 1, skip_abs, names, n, max, skip_platform);
        } else {
            size_t len = strlen(e->d_name);
            if (len < 4 || strcmp(e->d_name + len - 3, ".hy") != 0) continue;
            if (skip_abs && !strcmp(full, skip_abs)) continue;
            char mod[96];
            snprintf(mod, sizeof(mod), "%s%s%.*s", rel_dir, rel_dir[0] ? "/" : "", (int)(len - 3), e->d_name);
            for (char *p = mod; *p; p++) if (*p == '/') *p = '.';
            if (!name_seen(names, *n, mod)) snprintf(names[(*n)++], 96, "%s", mod);
        }
    }
    closedir(d);
}

int std_module_names_at(const char *root, char names[][96], int max) {
    int n = 0;
    if (root && root[0]) scan_hy_tree(root, "", 0, NULL, names, &n, max, 1);
    return n;
}

int std_module_names(char names[][96], int max) {
    /* a ZoraInfo-free variant: honour HYLIAN_LIB, the install, ~/.hylian */
    ZoraInfo z; memset(&z, 0, sizeof(z));
    /* workspace unknown here; callers wanting the repo fallback use std_root_for */
    char root[1024];
    std_root_for(&z, root, sizeof(root));
    int n = 0;
    if (root[0]) scan_hy_tree(root, "", 0, NULL, names, &n, max, 1);
    return n;
}

int project_module_names(const ZoraInfo *z, const char *current_path, char names[][96], int max) {
    int n = 0;
    scan_hy_tree(z->src_dir, "", 0, current_path, names, &n, max, 0);
    return n;
}

int vendor_module_names(const ZoraInfo *z, char names[][96], char *pending, int max) {
    int n = 0;
    DIR *d = opendir(z->vendors_dir);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) != NULL && n < max) {
            if (e->d_name[0] == '.') continue;
            char p[1500];
            size_t len = strlen(e->d_name);
            if (len > 4 && !strcmp(e->d_name + len - 4, ".hyi")) {
                snprintf(names[n], 96, "%.*s", (int)(len - 4), e->d_name);
                pending[n++] = 0;
                continue;
            }
            snprintf(p, sizeof(p), "%s/%s/%s.hyi", z->vendors_dir, e->d_name, e->d_name);
            if (module_exists(p)) { snprintf(names[n], 96, "%s", e->d_name); pending[n++] = 0; }
        }
        closedir(d);
    }
    for (int i = 0; i < z->dep_count && n < max; i++)
        if (!name_seen(names, n, z->deps[i])) { snprintf(names[n], 96, "%s", z->deps[i]); pending[n++] = 1; }
    return n;
}
