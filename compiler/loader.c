/* loader.c - everything about turning an `include { ... }` path into files.
 *
 * Shared by the compiler (compiler.c) and the language server so both resolve
 * `std.*`, `vendors.*` and project modules by exactly the same rules; the
 * server used to carry its own resolver and had drifted from the compiler's.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include "ast.h"
#include "loader.h"

extern int g_loc_l1, g_loc_c1, g_loc_l2, g_loc_c2;   /* see ast.c */

/* Given "/some/dir/file.hy" return "/some/dir" (heap-allocated).
   If there is no slash, returns "." */
char *dir_of(const char *path) {
    const char *last = strrchr(path, '/');
    if (!last) return strdup(".");
    int len = (int)(last - path);
    char *dir = malloc(len + 1);
    strncpy(dir, path, len);
    dir[len] = '\0';
    return dir;
}

/* Convert a dot-separated module path to a file path relative to src_dir.
   e.g. src_dir="src", module="Game.Player"  ->  "src/Game/Player.hy" */
char *module_to_filepath(const char *src_dir, const char *module) {
    int mlen = strlen(module);
    char *rel = malloc(mlen + 4); /* +4 for ".hy\0" */
    for (int i = 0; i < mlen; i++)
        rel[i] = (module[i] == '.') ? '/' : module[i];
    strcpy(rel + mlen, ".hy");
    int total = strlen(src_dir) + 1 + strlen(rel) + 1;
    char *full = malloc(total);
    snprintf(full, total, "%s/%s", src_dir, rel);
    free(rel);
    return full;
}

/* Resolve a dotted local include like "modules.cat.Cat" to a real .hy file
   under `dir`. Tries the full dotted path first (module_to_filepath's
   normal every-dot-is-a-slash behaviour, for a genuinely nested file
   layout), then progressively drops trailing components and retries -
   "modules/cat/Cat.hy" (doesn't exist), then "modules/cat.hy" (does).
   This is what lets `include { modules.cat.Cat }` work: callers now have to
   qualify calls into a module block as `Cat.catFunc(...)` (see lower.c /
   typecheck.c), so writing the include path the same way - naming the file
   *and* the module you want out of it - is the natural thing to type, even
   though the module name isn't actually part of the filesystem path.
   Returns a heap path (caller frees) that fopen'd successfully, or NULL if
   nothing along the chain exists. */
char *resolve_dotted_include(const char *dir, const char *module) {
    char *dep_path = module_to_filepath(dir, module);
    FILE *probe = fopen(dep_path, "r");
    if (probe) { fclose(probe); return dep_path; }
    free(dep_path);

    char *trimmed = strdup(module);
    char *last_dot;
    while ((last_dot = strrchr(trimmed, '.')) != NULL) {
        *last_dot = '\0';
        dep_path = module_to_filepath(dir, trimmed);
        probe = fopen(dep_path, "r");
        if (probe) { fclose(probe); free(trimmed); return dep_path; }
        free(dep_path);
    }
    free(trimmed);
    return NULL;
}




/* Trim leading and trailing whitespace in-place, returning pointer to first
   non-space character (the string is also NUL-terminated after the last
   non-space character). */
char *trim(char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)*(end - 1))) end--;
    *end = '\0';
    return s;
}

/* A type as written in a .hyi declaration: `int`, `float`, `str`, `*void`,
   `**uint8`, a struct name... */
static Type hyi_parse_type(const char *text) {
    char buf[128];
    strncpy(buf, text, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char *t = trim(buf);
    if (*t == '*') return make_rawptr_type(hyi_parse_type(t + 1));
    return make_simple_type(strdup(t), 0);
}

/* Parse the `name: type, name: type` list between the parentheses of a .hyi
   `fn` line into VarDeclNode parameters. They used to be discarded, which is
   why the compiler couldn't tell a C function's float arguments from its
   integer ones (SysV passes them in different registers). A parameter with no
   `name:` prefix is accepted as a bare type. */
static void hyi_parse_params(const char *open_paren, ASTNode ***out_params, int *out_count) {
    *out_params = NULL;
    *out_count = 0;
    const char *p = open_paren + 1;
    const char *end = NULL;
    int depth = 1;
    for (const char *q = p; *q; q++) {
        if (*q == '(') depth++;
        else if (*q == ')' && --depth == 0) { end = q; break; }
    }
    if (!end) return;
    char list[1024];
    size_t len = (size_t)(end - p);
    if (len >= sizeof(list)) len = sizeof(list) - 1;
    memcpy(list, p, len);
    list[len] = '\0';
    char *save = NULL;
    for (char *tok = strtok_r(list, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
        char *item = trim(tok);
        if (!*item) continue;
        char *colon = strchr(item, ':');
        char pname[64];
        const char *tname = item;
        if (colon) {
            *colon = '\0';
            snprintf(pname, sizeof(pname), "%s", trim(item));
            tname = colon + 1;
        } else {
            snprintf(pname, sizeof(pname), "arg%d", *out_count);
        }
        Type ty = hyi_parse_type(tname);
        *out_params = realloc(*out_params, (*out_count + 1) * sizeof(ASTNode *));
        (*out_params)[(*out_count)++] = (ASTNode *)make_var_decl(ty, strdup(pname), NULL);
    }
}

/* Parse a .hyi file into a synthetic ProgramNode.
   Also writes the first "link" library name into *out_link (heap-allocated),
   and collects all "pkg" directive names into out_pkgs / out_pkg_count.
   Callers must free *out_link and each out_pkgs[i] entry. */
ProgramNode *parse_hyi_into_program(const char *filepath, char **out_link,
                                            char ***out_pkgs, int *out_pkg_count) {
    *out_link      = NULL;
    *out_pkgs      = NULL;
    *out_pkg_count = 0;

    FILE *f = fopen(filepath, "r");
    if (!f) return NULL;

    ProgramNode *prog = make_program();

    char line[1024];
    ClassNode *current_class = NULL;
    enum { NO_BLOCK, CLASS_BLOCK, STRUCT_BLOCK, UNION_BLOCK } block_type = NO_BLOCK;

    int hyi_line = 0;
    while (fgets(line, sizeof(line), f)) {
        /* every declaration is stamped with the .hyi line it came from, so the
           editor can jump to it */
        hyi_line++;
        g_loc_l1 = g_loc_l2 = hyi_line;
        g_loc_c1 = 0; g_loc_c2 = (int)strlen(line);
        char *s = trim(line);
        if (*s == '\0') continue;
        if (strncmp(s, "module ", 7) == 0) continue;
        if (strncmp(s, "link ", 5) == 0) {
            if (*out_link == NULL) {
                char *q = strchr(s + 5, '"');
                if (q) {
                    q++; /* skip opening quote */
                    char *end = strchr(q, '"');
                    if (end) {
                        int len = (int)(end - q);
                        *out_link = malloc(len + 1);
                        strncpy(*out_link, q, len);
                        (*out_link)[len] = '\0';
                    }
                }
            }
            continue;
        }
        if (strncmp(s, "pkg ", 4) == 0) {
            char *q = strchr(s + 4, '"');
            if (q) {
                q++; /* skip opening quote */
                char *end = strchr(q, '"');
                if (end) {
                    int len = (int)(end - q);
                    char *pkg_name = malloc(len + 1);
                    strncpy(pkg_name, q, len);
                    pkg_name[len] = '\0';
                    *out_pkgs = realloc(*out_pkgs, (*out_pkg_count + 1) * sizeof(char *));
                    (*out_pkgs)[(*out_pkg_count)++] = pkg_name;
                }
            }
            continue;
        }

        /* const NAME = VALUE  — bindgen emits these for enum values */
        if (strncmp(s, "const ", 6) == 0) {
            char const_name[256];
            long  const_val = 0;
            char *eq = strchr(s + 6, '=');
            if (eq) {
                int name_len = (int)(eq - (s + 6));
                if (name_len > 0 && name_len < (int)sizeof(const_name)) {
                    strncpy(const_name, s + 6, name_len);
                    const_name[name_len] = '\0';
                    char *trimmed_name = trim(const_name);
                    char *val_str = trim(eq + 1);
                    /* handle hex (0x…) and decimal */
                    if (strncmp(val_str, "0x", 2) == 0 || strncmp(val_str, "0X", 2) == 0)
                        const_val = strtol(val_str, NULL, 16);
                    else
                        const_val = strtol(val_str, NULL, 10);
                    /* Register as a const static var so the typechecker and
                       codegen know about it and can load it by name. */
                    StaticVarNode *sv = malloc(sizeof(StaticVarNode));
                    memset(sv, 0, sizeof(*sv));
                    sv->base.type = NODE_STATIC_VAR;
                    sv->var_name  = strdup(trimmed_name);
                    sv->var_type  = make_simple_type(strdup("int"), 0);
                    sv->is_const  = 1;
                    sv->array_size = 0;
                    char val_buf[32];
                    snprintf(val_buf, sizeof(val_buf), "%ld", const_val);
                    LiteralNode *lit = make_literal(strdup(val_buf), LIT_INT);
                    sv->initializer = (ASTNode *)lit;
                    prog->declarations = realloc(
                        prog->declarations,
                        (prog->decl_count + 1) * sizeof(ASTNode *)
                    );
                    prog->declarations[prog->decl_count++] = (ASTNode *)sv;
                }
            }
            continue;
        }

        /* closing brace — end of class/struct block */
        if (strcmp(s, "}") == 0) {
            if (current_class) {
                prog->declarations = realloc(
                    prog->declarations,
                    (prog->decl_count + 1) * sizeof(ASTNode *)
                );
                prog->declarations[prog->decl_count++] = (ASTNode *)current_class;
                current_class = NULL;
                block_type = NO_BLOCK;
            }
            continue;
        }

        /* class Name {  or  class Name {}  (bindgen one-liner) */
        if (strncmp(s, "class ", 6) == 0) {
            char class_name[256];
            if (sscanf(s + 6, "%255s", class_name) == 1) {
                /* skip class names that start with "const" — these are
                   artefacts from bindgen emitting "class const SDL_Rect {}"
                   for const-pointer params; they are duplicates of the
                   non-const version and are not valid Hylian identifiers. */
                if (strncmp(class_name, "const", 5) == 0) continue;
                /* strip trailing "{" or "{}" */
                char *brace = strchr(class_name, '{');
                if (brace) *brace = '\0';
                char *trimmed_name = trim(class_name);
                if (*trimmed_name == '\0') continue;
                current_class = make_class(strdup(trimmed_name), 1);
                block_type = CLASS_BLOCK;
                /* one-liner: "class Name {}" — close immediately */
                if (strstr(s + 6, "{}")) {
                    prog->declarations = realloc(
                        prog->declarations,
                        (prog->decl_count + 1) * sizeof(ASTNode *)
                    );
                    prog->declarations[prog->decl_count++] = (ASTNode *)current_class;
                    current_class = NULL;
                    block_type = NO_BLOCK;
                }
            }
            continue;
        }

        /* struct Name {  or  struct Name {}  (one-liner) */
        if (strncmp(s, "struct ", 7) == 0) {
            char struct_name[256];
            if (sscanf(s + 7, "%255s", struct_name) == 1) {
                char *brace = strchr(struct_name, '{');
                if (brace) *brace = '\0';
                char *trimmed_name = trim(struct_name);
                if (*trimmed_name == '\0') continue;
                current_class = make_class(strdup(trimmed_name), 0);
                current_class->has_ctor = 0;
                block_type = STRUCT_BLOCK;
                /* one-liner empty struct */
                if (strstr(s + 7, "{}")) {
                    prog->declarations = realloc(
                        prog->declarations,
                        (prog->decl_count + 1) * sizeof(ASTNode *)
                    );
                    prog->declarations[prog->decl_count++] = (ASTNode *)current_class;
                    current_class = NULL;
                    block_type = NO_BLOCK;
                }
            }
            continue;
        }

        /* union class Name {  or  union class Name {}  (one-liner) */
        if (strncmp(s, "union class ", 12) == 0) {
            char union_name[256];
            if (sscanf(s + 12, "%255s", union_name) == 1) {
                char *brace = strchr(union_name, '{');
                if (brace) *brace = '\0';
                char *trimmed_name = trim(union_name);
                if (*trimmed_name == '\0') goto next_line_union;
                current_class = make_class(strdup(trimmed_name), 0);
                current_class->has_ctor = 0;
                current_class->is_union = 1;
                block_type = UNION_BLOCK;
                /* one-liner empty union */
                if (strstr(s + 12, "{}")) {
                    prog->declarations = realloc(
                        prog->declarations,
                        (prog->decl_count + 1) * sizeof(ASTNode *)
                    );
                    prog->declarations[prog->decl_count++] = (ASTNode *)current_class;
                    current_class = NULL;
                    block_type = NO_BLOCK;
                }
            }
            next_line_union:;
            continue;
        }

        /* Inside a struct/union block: field lines are "fieldtype fieldname" */
        if ((block_type == STRUCT_BLOCK || block_type == UNION_BLOCK) && current_class && strncmp(s, "fn ", 3) != 0) {
            char fieldtype_str[256], fieldname_str[256];
            if (sscanf(s, "%255s %255s", fieldtype_str, fieldname_str) == 2) {
                /* Check for fixed-size array syntax: type[N] e.g. uint8[56] */
                Type ft;
                char *bracket = strchr(fieldtype_str, '[');
                if (bracket) {
                    int arr_size = 0;
                    *bracket = '\0';
                    sscanf(bracket + 1, "%d", &arr_size);
                    Type elem = make_simple_type(strdup(fieldtype_str), 0);
                    ft = make_array_type(elem, arr_size > 0 ? arr_size : 1);
                } else {
                    ft = make_simple_type(strdup(fieldtype_str), 0);
                }
                FieldNode *field_node = make_field(ft, fieldname_str, 1);
                current_class->fields = realloc(
                    current_class->fields,
                    (current_class->field_count + 1) * sizeof(FieldNode *)
                );
                current_class->fields[current_class->field_count++] = field_node;
            }
            continue;
        }

        /* fn name(...) -> rettype  or  fn name(...) */
        if (strncmp(s, "fn ", 3) == 0) {
            char *rest = s + 3;

            /* Extract function name (up to '(') */
            char *paren = strchr(rest, '(');
            if (!paren) continue;
            int name_len = (int)(paren - rest);
            char func_name[256];
            if (name_len <= 0 || name_len >= (int)sizeof(func_name)) continue;
            strncpy(func_name, rest, name_len);
            func_name[name_len] = '\0';
            char *fn_name = trim(func_name);

            /* Extract return type after "->" if present */
            Type ret_type;
            char *arrow = strstr(paren, "->");
            if (arrow) {
                char ret_str[256];
                strncpy(ret_str, arrow + 2, sizeof(ret_str) - 1);
                ret_str[sizeof(ret_str) - 1] = '\0';
                char *trimmed_ret = trim(ret_str);
                ret_type = hyi_parse_type(trimmed_ret);
            } else {
                ret_type = make_simple_type(strdup("void"), 0);
            }

            if (current_class && block_type == STRUCT_BLOCK) {
                /* fn inside a struct block → warn and ignore */
                fprintf(stderr, "warning: 'fn' line ignored inside struct block '%s'\n",
                        current_class->name);
            } else if (current_class) {
                MethodNode *m = make_method(ret_type, strdup(fn_name));
                hyi_parse_params(paren, &m->params, &m->param_count);
                m->body_count  = 0;
                current_class->methods = realloc(
                    current_class->methods,
                    (current_class->method_count + 1) * sizeof(MethodNode *)
                );
                current_class->methods[current_class->method_count++] = m;
            } else {
                FuncNode *fn = make_func(ret_type, strdup(fn_name));
                hyi_parse_params(paren, &fn->params, &fn->param_count);
                fn->body_count  = 0;
                prog->declarations = realloc(
                    prog->declarations,
                    (prog->decl_count + 1) * sizeof(ASTNode *)
                );
                prog->declarations[prog->decl_count++] = (ASTNode *)fn;
            }
            continue;
        }
    }

    fclose(f);
    g_loc_l1 = g_loc_c1 = g_loc_l2 = g_loc_c2 = 0;
    return prog;
}



/* ── include resolution ─────────────────────────────────────────────────── */

void loader_installed_std_dir(char *buf, size_t bufsz) {
    const char *hylian_lib = getenv("HYLIAN_LIB");
    if (hylian_lib) {
        snprintf(buf, bufsz, "%s/std", hylian_lib);
        struct stat st;
        if (stat(buf, &st) != 0 || !S_ISDIR(st.st_mode))
            snprintf(buf, bufsz, "%s", hylian_lib);
    } else {
        snprintf(buf, bufsz, "%s", "/usr/local/lib/hylian/std");
    }
}

static int file_exists(const char *p) {
    FILE *f = fopen(p, "r");
    if (!f) return 0;
    fclose(f);
    return 1;
}

/* dots -> slashes */
static char *dots_to_slashes(const char *s) {
    char *r = strdup(s);
    for (char *p = r; *p; p++) if (*p == '.') *p = '/';
    return r;
}

/* first candidate that exists, as a heap string */
static char *first_existing(char **cands, int n) {
    for (int i = 0; i < n; i++)
        if (cands[i] && file_exists(cands[i])) return strdup(cands[i]);
    return NULL;
}

static char *fmt_path(const char *fmt, const char *a, const char *b, const char *c) {
    size_t n = strlen(fmt) + (a ? strlen(a) : 0) + (b ? strlen(b) : 0) + (c ? strlen(c) : 0) + 1;
    char *s = malloc(n);
    snprintf(s, n, fmt, a ? a : "", b ? b : "", c ? c : "");
    return s;
}

void loader_resolve_include(const char *inc, const char *src_dir, const char *file_dir,
                            ResolvedInclude *out) {
    memset(out, 0, sizeof(*out));

    if (strncmp(inc, "link:", 5) == 0 || strncmp(inc, "pkg:", 4) == 0 ||
        strcmp(inc, "kernel") == 0) {
        out->kind = INC_SKIP;
        return;
    }

    if (strncmp(inc, "std.", 4) == 0) {
        out->kind = INC_STD;
        char *rel = dots_to_slashes(inc + 4);
        char *project_root = dir_of(src_dir);
        char inst_std[1024];
        loader_installed_std_dir(inst_std, sizeof(inst_std));

        /* source-tree layout, relative layout, src_dir IS the std dir,
           src_dir/std, then the installed toolchain */
        char *roots[5] = {
            fmt_path("%s/runtime/std", project_root, NULL, NULL),
            strdup("runtime/std"),
            strdup(src_dir),
            fmt_path("%s/std", src_dir, NULL, NULL),
            strdup(inst_std),
        };
        char *hyi_c[5], *hy_c[5];
        for (int i = 0; i < 5; i++) {
            hyi_c[i] = fmt_path("%s/%s.hyi", roots[i], rel, NULL);
            hy_c[i]  = fmt_path("%s/%s.hy",  roots[i], rel, NULL);
        }
        out->hyi = first_existing(hyi_c, 5);
        for (int i = 0; i < 5; i++)
            if (file_exists(hy_c[i])) {
                out->hy = strdup(hy_c[i]);
                /* sibling includes inside nested std modules resolve against
                   the std ROOT, not the file's own directory */
                out->hy_root = strdup(roots[i]);
                break;
            }
        for (int i = 0; i < 5; i++) { free(roots[i]); free(hyi_c[i]); free(hy_c[i]); }
        free(rel); free(project_root);
        return;
    }

    if (strncmp(inc, "vendors.", 8) == 0) {
        out->kind = INC_VENDOR;
        char *rel = dots_to_slashes(inc + 8);
        const char *leaf = strrchr(rel, '/');
        leaf = leaf ? leaf + 1 : rel;
        char *project_root = dir_of(src_dir);
        /* project-root layout first, then src_dir-relative */
        char *c[4] = {
            fmt_path("%s/vendors/%s/%s.hyi", project_root, rel, leaf),
            fmt_path("%s/vendors/%s.hyi",    project_root, rel, NULL),
            fmt_path("%s/vendors/%s/%s.hyi", src_dir, rel, leaf),
            fmt_path("%s/vendors/%s.hyi",    src_dir, rel, NULL),
        };
        out->hyi = first_existing(c, 4);
        /* keep the primary candidate for "cannot open" messages */
        if (!out->hyi) out->hyi = NULL;
        for (int i = 0; i < 4; i++) free(c[i]);
        free(rel); free(project_root);
        return;
    }

    out->kind = INC_FILE;
    char *dep = resolve_dotted_include(src_dir, inc);
    if (!dep) dep = resolve_dotted_include(file_dir, inc);
    if (!dep) dep = module_to_filepath(src_dir, inc);
    out->hy = dep;
}

void loader_free_resolved(ResolvedInclude *r) {
    free(r->hyi); free(r->hy); free(r->hy_root);
    memset(r, 0, sizeof(*r));
}
