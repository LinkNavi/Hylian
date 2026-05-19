#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "ast.h"
#include "codegen_asm.h"
#include "ir.h"
#include "lower.h"
#include "opt.h"
#include "typecheck.h"


extern int  yyparse(void);
extern void yyrestart(FILE *f);
extern int  yylineno;
extern ProgramNode *root;

/* Defined and owned by parser.y — set before each yyparse() call */
extern const char *current_parse_file;


#define MAX_VISITED 256

static char *visited[MAX_VISITED];
static int   visited_count = 0;

static int already_visited(const char *path) {
    for (int i = 0; i < visited_count; i++)
        if (strcmp(visited[i], path) == 0) return 1;
    return 0;
}

static void mark_visited(const char *path) {
    if (visited_count < MAX_VISITED)
        visited[visited_count++] = strdup(path);
}


/* Given "/some/dir/file.hy" return "/some/dir" (heap-allocated).
   If there is no slash, returns "." */
static char *dir_of(const char *path) {
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
static char *module_to_filepath(const char *src_dir, const char *module) {
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


static ProgramNode *compile_file(const char *filepath, const char *src_dir);


/* Trim leading and trailing whitespace in-place, returning pointer to first
   non-space character (the string is also NUL-terminated after the last
   non-space character). */
static char *trim(char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)*(end - 1))) end--;
    *end = '\0';
    return s;
}

/* Parse a .hyi file into a synthetic ProgramNode.
   Also writes the first "link" library name into *out_link (heap-allocated),
   and collects all "pkg" directive names into out_pkgs / out_pkg_count.
   Callers must free *out_link and each out_pkgs[i] entry. */
static ProgramNode *parse_hyi_into_program(const char *filepath, char **out_link,
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

    while (fgets(line, sizeof(line), f)) {
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
                ret_type = make_simple_type(strdup(trimmed_ret), 0);
            } else {
                ret_type = make_simple_type(strdup("void"), 0);
            }

            if (current_class && block_type == STRUCT_BLOCK) {
                /* fn inside a struct block → warn and ignore */
                fprintf(stderr, "warning: 'fn' line ignored inside struct block '%s'\n",
                        current_class->name);
            } else if (current_class) {
                MethodNode *m = make_method(ret_type, strdup(fn_name));
                m->param_count = 0;
                m->body_count  = 0;
                current_class->methods = realloc(
                    current_class->methods,
                    (current_class->method_count + 1) * sizeof(MethodNode *)
                );
                current_class->methods[current_class->method_count++] = m;
            } else {
                FuncNode *fn = make_func(ret_type, strdup(fn_name));
                fn->param_count = 0;
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
    return prog;
}


static void merge_programs(ProgramNode *dst, ProgramNode *src) {
    for (int i = 0; i < src->decl_count; i++) {
        dst->declarations = realloc(
            dst->declarations,
            (dst->decl_count + 1) * sizeof(ASTNode *)
        );
        dst->declarations[dst->decl_count++] = src->declarations[i];
    }
    /* Merge include paths (preserved for codegen's std module resolver) */
    for (int i = 0; i < src->include_count; i++) {
        int dup = 0;
        for (int j = 0; j < dst->include_count; j++)
            if (strcmp(dst->includes[j], src->includes[i]) == 0) { dup = 1; break; }
        if (!dup) {
            dst->includes = realloc(dst->includes, (dst->include_count + 1) * sizeof(char *));
            dst->includes[dst->include_count++] = src->includes[i];
        }
    }
    for (int i = 0; i < src->cpp_include_count; i++) {
        int dup = 0;
        for (int j = 0; j < dst->cpp_include_count; j++)
            if (strcmp(dst->cpp_includes[j], src->cpp_includes[i]) == 0) { dup = 1; break; }
        if (!dup) {
            dst->cpp_includes = realloc(dst->cpp_includes, (dst->cpp_include_count + 1) * sizeof(char *));
            dst->cpp_includes[dst->cpp_include_count++] = src->cpp_includes[i];
        }
    }
}

/* Returns a ProgramNode with all transitive declarations merged in,
   or NULL on error. */
static ProgramNode *compile_file(const char *filepath, const char *src_dir) {
    if (already_visited(filepath)) return NULL;
    mark_visited(filepath);

    FILE *f = fopen(filepath, "r");
    if (!f) {
        fprintf(stderr, "hylian: cannot open '%s'\n", filepath);
        return NULL;
    }

    /* Reset parser state and parse the file */
    root = NULL;
    yyrestart(f);
    yylineno = 1;
    current_parse_file = filepath;
    int parse_result = yyparse();
    fclose(f);

    if (parse_result != 0 || !root) {
        fprintf(stderr, "hylian: failed to parse '%s'\n", filepath);
        return NULL;
    }

    ProgramNode *program = root;
    root = NULL;

    /* Determine the directory this file lives in — used to resolve its imports */
    char *file_dir = dir_of(filepath);

    /* Snapshot the include list before walking it. As we resolve dependencies
       and merge them into `program`, `program->includes` will grow with
       transitive includes — but we only want to resolve the ORIGINAL includes
       of this file here. Without the snapshot, we'd reprocess transitive deps
       (and potentially infinite-loop on diamond imports) because the loop
       bound `program->include_count` keeps growing as we merge. */
    int   orig_include_count = program->include_count;
    char **orig_includes = NULL;
    if (orig_include_count > 0) {
        orig_includes = malloc(orig_include_count * sizeof(char *));
        for (int i = 0; i < orig_include_count; i++)
            orig_includes[i] = program->includes[i];
    }

    /* Walk the snapshotted include list and resolve non-std paths */
    for (int i = 0; i < orig_include_count; i++) {
        const char *inc = orig_includes[i];

        /* std.* paths are resolved as .hyi interface files from runtime/std/
           so the typecheck pass knows about their function signatures.
           The codegen also handles them separately for linking. */
        if (strncmp(inc, "std.", 4) == 0) {
            /* Convert std.foo.bar -> runtime/std/foo/bar.hyi
               Strip the "std." prefix first */
            const char *std_module = inc + 4; /* e.g. "io", "kernel", "networking.tcp" */
            int slen = strlen(std_module);
            char *std_rel = malloc(slen + 1);
            for (int j = 0; j < slen; j++)
                std_rel[j] = (std_module[j] == '.') ? '/' : std_module[j];
            std_rel[slen] = '\0';

            /* Build candidate paths:
               1. <project_root>/runtime/std/<std_rel>.hyi  (from compiler binary dir)
               2. runtime/std/<std_rel>.hyi                 (relative to cwd) */
            char *project_root = dir_of(src_dir);

            char *hyi1 = malloc(strlen(project_root) + 14 + slen + 5);
            sprintf(hyi1, "%s/runtime/std/%s.hyi", project_root, std_rel);

            char *hyi2 = malloc(14 + slen + 5);
            sprintf(hyi2, "runtime/std/%s.hyi", std_rel);

            free(project_root);
            free(std_rel);

            char *hyi_path = NULL;
            const char *cands[] = { hyi1, hyi2, NULL };
            for (int ci = 0; cands[ci]; ci++) {
                FILE *probe = fopen(cands[ci], "r");
                if (probe) { fclose(probe); hyi_path = strdup(cands[ci]); break; }
            }
            free(hyi1); free(hyi2);

            if (hyi_path && !already_visited(hyi_path)) {
                mark_visited(hyi_path);
                char *link_lib  = NULL;
                char **pkg_names = NULL;
                int    pkg_count = 0;
                ProgramNode *dep = parse_hyi_into_program(hyi_path, &link_lib,
                                                           &pkg_names, &pkg_count);
                if (dep) {
                    /* Keep the original std.* include in place for codegen */
                    dep->includes = realloc(dep->includes, (dep->include_count + 1) * sizeof(char *));
                    dep->includes[dep->include_count++] = strdup(inc);
                    merge_programs(program, dep);
                }
                if (link_lib) free(link_lib);
                for (int pi = 0; pi < pkg_count; pi++) free(pkg_names[pi]);
                free(pkg_names);
            }
            if (hyi_path) free(hyi_path);
            continue;
        }

        /* link:* and pkg:* synthetic includes are codegen linker hints, not files */
        if (strncmp(inc, "link:", 5) == 0) continue;
        if (strncmp(inc, "pkg:",  4) == 0) continue;

        /* vendors.* paths are resolved as .hyi interface files */
        if (strncmp(inc, "vendors.", 8) == 0) {
            const char *vendor_module = inc + 8; /* e.g. "vendors.vulkan" -> "vulkan" */
            int vlen = strlen(vendor_module);
            char *vendor_rel = malloc(vlen + 1);
            for (int j = 0; j < vlen; j++)
                vendor_rel[j] = (vendor_module[j] == '.') ? '/' : vendor_module[j];
            vendor_rel[vlen] = '\0';

            /* Get the last component (leaf name) for "vendors/foo/foo.hyi" layout */
            const char *leaf = strrchr(vendor_rel, '/');
            leaf = leaf ? leaf + 1 : vendor_rel;

            /* The vendors/ directory lives at the project root, which is the
               parent of src_dir (e.g. src_dir="myproj/src" -> root="myproj").
               Try project-root-relative paths first, then src_dir-relative as
               a fallback for projects where src == root. */
            char *project_root = dir_of(src_dir);

            /* Candidate paths (prefer project-root layout):
               1. <project_root>/vendors/<vendor_rel>/<leaf>.hyi
               2. <project_root>/vendors/<vendor_rel>.hyi
               3. <src_dir>/vendors/<vendor_rel>/<leaf>.hyi
               4. <src_dir>/vendors/<vendor_rel>.hyi */
            int pr_len  = strlen(project_root);
            int src_len = strlen(src_dir);

            char *hyi_path1 = malloc(pr_len  + 10 + vlen + 1 + strlen(leaf) + 5);
            sprintf(hyi_path1, "%s/vendors/%s/%s.hyi", project_root, vendor_rel, leaf);

            char *hyi_path2 = malloc(pr_len  + 10 + vlen + 5);
            sprintf(hyi_path2, "%s/vendors/%s.hyi", project_root, vendor_rel);

            char *hyi_path3 = malloc(src_len + 10 + vlen + 1 + strlen(leaf) + 5);
            sprintf(hyi_path3, "%s/vendors/%s/%s.hyi", src_dir, vendor_rel, leaf);

            char *hyi_path4 = malloc(src_len + 10 + vlen + 5);
            sprintf(hyi_path4, "%s/vendors/%s.hyi", src_dir, vendor_rel);

            free(vendor_rel);
            free(project_root);

            /* Pick first candidate that exists */
            char *hyi_path = NULL;
            const char *candidates[] = { hyi_path1, hyi_path2, hyi_path3, hyi_path4, NULL };
            for (int ci = 0; candidates[ci]; ci++) {
                FILE *probe = fopen(candidates[ci], "r");
                if (probe) {
                    fclose(probe);
                    hyi_path = strdup(candidates[ci]);
                    break;
                }
            }
            free(hyi_path1); free(hyi_path2); free(hyi_path3); free(hyi_path4);

            if (already_visited(hyi_path)) { free(hyi_path); continue; }
            mark_visited(hyi_path);

            char *link_lib  = NULL;
            char **pkg_names = NULL;
            int    pkg_count = 0;
            ProgramNode *dep = parse_hyi_into_program(hyi_path, &link_lib,
                                                       &pkg_names, &pkg_count);

            if (!dep) {
                fprintf(stderr, "hylian: cannot open vendor interface '%s'\n", hyi_path);
                free(hyi_path);
                continue;
            }
            free(hyi_path);

            /* Surface the link directive as a synthetic include "link:<libname>"
               so codegen can detect and emit the appropriate linker flag. */
            if (link_lib) {
                int link_inc_len = 5 + strlen(link_lib) + 1; /* "link:" + lib + NUL */
                char *link_inc = malloc(link_inc_len);
                snprintf(link_inc, link_inc_len, "link:%s", link_lib);
                free(link_lib);

                /* Add to dep so it bubbles up through merge_programs */
                dep->includes = realloc(dep->includes, (dep->include_count + 1) * sizeof(char *));
                dep->includes[dep->include_count++] = link_inc;
            }

            /* Surface each pkg directive as a synthetic include "pkg:<name>"
               so codegen can emit it in the link hint comment. */
            for (int pi = 0; pi < pkg_count; pi++) {
                int pkg_inc_len = 4 + strlen(pkg_names[pi]) + 1; /* "pkg:" + name + NUL */
                char *pkg_inc = malloc(pkg_inc_len);
                snprintf(pkg_inc, pkg_inc_len, "pkg:%s", pkg_names[pi]);
                free(pkg_names[pi]);
                dep->includes = realloc(dep->includes, (dep->include_count + 1) * sizeof(char *));
                dep->includes[dep->include_count++] = pkg_inc;
            }
            free(pkg_names);

            /* Also carry the original vendors.* include string forward so
               codegen can see it in the final program's include list. */
            dep->includes = realloc(dep->includes, (dep->include_count + 1) * sizeof(char *));
            dep->includes[dep->include_count++] = strdup(inc);

            merge_programs(program, dep);
            continue;
        }

        /* Resolve to a file path — try src_dir first (absolute module paths
           like "boot.tss" or "mm.pmm"), then fall back to the file's own
           directory (for legacy flat layouts). */
        char *dep_path = module_to_filepath(src_dir, inc);
        FILE *dep_probe = fopen(dep_path, "r");
        if (!dep_probe) {
            free(dep_path);
            dep_path = module_to_filepath(file_dir, inc);
        } else {
            fclose(dep_probe);
        }

        /* Recursively compile the dependency, always passing src_dir so
           transitive includes are resolved from the same root. */
        ProgramNode *dep = compile_file(dep_path, src_dir);
        free(dep_path);

        if (!dep) continue; /* error already printed */

        /* Merge dep into `program` in-place. Previously this built a new
           `merged` ProgramNode and reassigned `program = merged`, but that
           silently re-pointed the loop iterator at a different include list
           (with dep's transitive includes prepended), causing duplicate
           processing and lost decls on files with 2+ includes. */
        merge_programs(program, dep);
    }

    if (orig_includes) free(orig_includes);
    free(file_dir);
    return program;
}


int main(int argc, char **argv) {
    const char *input_file  = NULL;
    const char *output_file = NULL;
    const char *src_dir     = ".";
    const char *target      = "linux";
    int dump_ir = 0;
    int freestanding = 0;

    /* Parse arguments:
         hylian <input.hy>
         hylian <input.hy> -o <output.asm>
         hylian <input.hy> -o <output.asm> --src-dir <dir>
         hylian <input.hy> --target <linux|macos|windows>
         hylian <input.hy> --dump-ir
         hylian <input.hy> --freestanding */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_file = argv[++i];
        } else if (strcmp(argv[i], "--src-dir") == 0 && i + 1 < argc) {
            src_dir = argv[++i];
        } else if (strcmp(argv[i], "--target") == 0 && i + 1 < argc) {
            target = argv[++i];
            if (strcmp(target, "linux") != 0 &&
                strcmp(target, "macos") != 0 &&
                strcmp(target, "windows") != 0 &&
                strcmp(target, "limine") != 0) {
                fprintf(stderr, "hylian: unknown target '%s' (must be linux, macos, windows, or limine)\n", target);
                return 1;
            }
        } else if (strcmp(argv[i], "--dump-ir") == 0) {
            dump_ir = 1;
        } else if (strcmp(argv[i], "--freestanding") == 0) {
            freestanding = 1;
        } else if (argv[i][0] != '-') {
            input_file = argv[i];
        } else {
            fprintf(stderr, "hylian: unknown option '%s'\n", argv[i]);
            fprintf(stderr, "usage: hylian <input.hy> [-o output.asm] [--src-dir dir] [--target linux|macos|windows|limine] [--dump-ir] [--freestanding]\n");
            return 1;
        }
    }

    if (!output_file)
        output_file = "output.asm";

    if (!input_file) {
        fprintf(stderr, "hylian: no input file specified\n");
        fprintf(stderr, "usage: hylian <input.hy> [-o output.asm] [--src-dir dir] [--target linux|macos|windows|limine] [--dump-ir]\n");
        return 1;
    }

    if (strcmp(src_dir, ".") == 0) {
        src_dir = dir_of(input_file);
    }

    ProgramNode *program = compile_file(input_file, src_dir);
    if (!program) return 1;

    typecheck(program, input_file);

    IRModule *mod = lower_program(program);
    if (dump_ir) {
        fprintf(stderr, "=== IR before optimization ===\n");
        ir_dump(mod, stderr);
        fprintf(stderr, "\n");
    }

    int changes = opt_run_all(mod);
    if (dump_ir) {
        fprintf(stderr, "=== IR after optimization (%d change%s) ===\n",
                changes, changes == 1 ? "" : "s");
        ir_dump(mod, stderr);
        fprintf(stderr, "\n");
    }

    FILE *out = fopen(output_file, "w");
    if (!out) {
        fprintf(stderr, "hylian: cannot open output file '%s'\n", output_file);
        ir_module_free(mod);
        return 1;
    }

    codegen_ir(mod, out, input_file, target, freestanding);
    fclose(out);
    ir_module_free(mod);
    printf("Generated %s\n", output_file);
    return 0;
}
