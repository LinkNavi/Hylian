#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include "ast.h"
#include "codegen_hygen.h"
#include "ir.h"
#include "lower.h"
#include "opt.h"
#include "typecheck.h"
#include "loader.h"
#include "diag.h"


extern int  yyparse(void);
extern void yyrestart(FILE *f);
extern int  yylineno;
extern ProgramNode *root;

/* Defined and owned by parser.y — set before each yyparse() call */
extern const char *current_parse_file;
extern const char *current_compile_target;


#define MAX_VISITED 256

static const char *current_target = "linux";
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


static void merge_programs(ProgramNode *dst, ProgramNode *src) {
    for (int i = 0; i < src->decl_count; i++) {
        dst->declarations = realloc(
            dst->declarations,
            (dst->decl_count + 1) * sizeof(ASTNode *)
        );
        /* Everything arriving via merge came from an include, by definition —
           this is the only path by which a declaration enters a program from
           another file. Recording it here is what later lets the optimizer
           drop stdlib functions the program never calls. */
        if (src->declarations[i])
            src->declarations[i]->from_include = 1;
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
    current_compile_target = current_target;
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

    /* Walk the snapshotted include list. Where each path lives is decided by
       loader_resolve_include(), the same resolver the language server uses. */
    for (int i = 0; i < orig_include_count; i++) {
        const char *inc = orig_includes[i];
        ResolvedInclude ri;
        loader_resolve_include(inc, src_dir, file_dir, &ri);

        if (ri.kind == INC_SKIP) { loader_free_resolved(&ri); continue; }

        if (ri.kind == INC_STD) {
            /* the interface file tells the typechecker each function's signature */
            if (ri.hyi && !already_visited(ri.hyi)) {
                mark_visited(ri.hyi);
                char *link_lib = NULL; char **pkg_names = NULL; int pkg_count = 0;
                ProgramNode *dep = parse_hyi_into_program(ri.hyi, &link_lib, &pkg_names, &pkg_count);
                if (dep) {
                    dep->includes = realloc(dep->includes, (dep->include_count + 1) * sizeof(char *));
                    dep->includes[dep->include_count++] = strdup(inc);
                    merge_programs(program, dep);
                }
                if (link_lib) free(link_lib);
                for (int pi = 0; pi < pkg_count; pi++) free(pkg_names[pi]);
                free(pkg_names);
            }
            /* a .hy next to it is pure-Hylian stdlib: compile it in */
            if (ri.hy && !already_visited(ri.hy)) {
                ProgramNode *dep = compile_file(ri.hy, ri.hy_root);
                if (dep) merge_programs(program, dep);
            }
            loader_free_resolved(&ri);
            continue;
        }

        if (ri.kind == INC_VENDOR) {
            if (!ri.hyi) {
                fprintf(stderr, "hylian: cannot open vendor interface for '%s' "
                                "(looked for vendors/<name>/<name>.hyi under the project root)\n", inc);
                loader_free_resolved(&ri);
                continue;
            }
            if (already_visited(ri.hyi)) { loader_free_resolved(&ri); continue; }
            mark_visited(ri.hyi);

            char *link_lib = NULL; char **pkg_names = NULL; int pkg_count = 0;
            ProgramNode *dep = parse_hyi_into_program(ri.hyi, &link_lib, &pkg_names, &pkg_count);
            if (!dep) {
                fprintf(stderr, "hylian: cannot open vendor interface '%s'\n", ri.hyi);
                loader_free_resolved(&ri);
                continue;
            }

            /* Surface the link directive as a synthetic include "link:<lib>"
               so codegen can emit the linker flag. */
            if (link_lib) {
                size_t n = 5 + strlen(link_lib) + 1;
                char *link_inc = malloc(n);
                snprintf(link_inc, n, "link:%s", link_lib);
                free(link_lib);
                dep->includes = realloc(dep->includes, (dep->include_count + 1) * sizeof(char *));
                dep->includes[dep->include_count++] = link_inc;
            }
            /* ...and each pkg directive as "pkg:<name>" */
            for (int pi = 0; pi < pkg_count; pi++) {
                size_t n = 4 + strlen(pkg_names[pi]) + 1;
                char *pkg_inc = malloc(n);
                snprintf(pkg_inc, n, "pkg:%s", pkg_names[pi]);
                free(pkg_names[pi]);
                dep->includes = realloc(dep->includes, (dep->include_count + 1) * sizeof(char *));
                dep->includes[dep->include_count++] = pkg_inc;
            }
            free(pkg_names);

            /* carry the original vendors.* include forward for codegen */
            dep->includes = realloc(dep->includes, (dep->include_count + 1) * sizeof(char *));
            dep->includes[dep->include_count++] = strdup(inc);

            merge_programs(program, dep);
            loader_free_resolved(&ri);
            continue;
        }

        /* a project module: compile it recursively, always passing src_dir so
           transitive includes resolve from the same root */
        ProgramNode *dep = compile_file(ri.hy, src_dir);
        loader_free_resolved(&ri);
        if (!dep) continue; /* error already printed */
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
         hylian <input.hy> -o <output.o>
         hylian <input.hy> -o <output.o> --src-dir <dir>
         hylian <input.hy> --target <linux|macos|windows|limine>
         hylian <input.hy> --dump-ir
         hylian <input.hy> --freestanding */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_file = argv[++i];
        } else if (strcmp(argv[i], "--src-dir") == 0 && i + 1 < argc) {
            src_dir = argv[++i];
        } else if (strcmp(argv[i], "--target") == 0 && i + 1 < argc) {
            target = argv[++i];
            current_target = target;
            if (strcmp(target, "linux") != 0 &&
                strcmp(target, "macos") != 0 &&
                strcmp(target, "windows") != 0 &&
                strcmp(target, "limine") != 0 &&
                strcmp(target, "termina") != 0) {
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
            fprintf(stderr, "usage: hylian <input.hy> [-o output.o] [--src-dir dir] [--target linux|macos|windows|limine] [--dump-ir] [--freestanding]\n");
            return 1;
        }
    }

    if (!output_file)
        output_file = "output.o";

    if (!input_file) {
        fprintf(stderr, "hylian: no input file specified\n");
        fprintf(stderr, "usage: hylian <input.hy> [-o output.o] [--src-dir dir] [--target linux|macos|windows|limine] [--dump-ir]\n");
        return 1;
    }

    if (strcmp(src_dir, ".") == 0) {
        src_dir = dir_of(input_file);
    }

    ProgramNode *program = compile_file(input_file, src_dir);
    if (!program) return 1;
    /* The parser recovers from syntax errors so it can report several at
       once - but a program with errors must not produce an object file. */
    if (diag_error_count() > 0) return 1;

    typecheck(program, input_file);
    if (diag_error_count() > 0) {
        fprintf(stderr, "hylian: %d error%s, no output written\n",
                diag_error_count(), diag_error_count() == 1 ? "" : "s");
        return 1;
    }

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

    /* hygen always emits a linkable ELF64 object; there's no separate
       "asm text" output mode anymore, so -o without --emit-elf should
       still land on a .o rather than the old .asm default. */
    if (codegen_hygen(mod, output_file, input_file, target, freestanding) != 0) {
        ir_module_free(mod);
        return 1;
    }
    ir_module_free(mod);
    printf("Generated %s\n", output_file);
    return 0;
}
