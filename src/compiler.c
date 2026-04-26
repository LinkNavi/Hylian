#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "codegen_asm.h"
#include "typecheck.h"
#include <string.h>

/* ─── External parser interface ─────────────────────────────────────────────── */

extern int  yyparse(void);
extern void yyrestart(FILE *f);
extern int  yylineno;
extern ProgramNode *root;

void yyerror(const char *s) {
    fprintf(stderr, "Error line %d: %s\n", yylineno, s);
}

/* ─── Visited-file set (avoid compiling the same file twice) ─────────────────── */

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

/* ─── Path helpers ───────────────────────────────────────────────────────────── */

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
    /* replace dots with slashes */
    int mlen = strlen(module);
    char *rel = malloc(mlen + 4); /* +4 for ".hy\0" */
    for (int i = 0; i < mlen; i++)
        rel[i] = (module[i] == '.') ? '/' : module[i];
    strcpy(rel + mlen, ".hy");

    /* join src_dir + "/" + rel */
    int total = strlen(src_dir) + 1 + strlen(rel) + 1;
    char *full = malloc(total);
    snprintf(full, total, "%s/%s", src_dir, rel);
    free(rel);
    return full;
}

/* ─── Forward declaration ────────────────────────────────────────────────────── */

static ProgramNode *compile_file(const char *filepath, const char *src_dir);

/* ─── Merge src program into dst ─────────────────────────────────────────────── */

static void merge_programs(ProgramNode *dst, ProgramNode *src) {
    /* Append all declarations from src into dst */
    for (int i = 0; i < src->decl_count; i++) {
        dst->declarations = realloc(
            dst->declarations,
            (dst->decl_count + 1) * sizeof(ASTNode *)
        );
        dst->declarations[dst->decl_count++] = src->declarations[i];
    }
    /* Merge include paths (for the std resolver in codegen) */
    for (int i = 0; i < src->include_count; i++) {
        int dup = 0;
        for (int j = 0; j < dst->include_count; j++)
            if (strcmp(dst->includes[j], src->includes[i]) == 0) { dup = 1; break; }
        if (!dup) {
            dst->includes = realloc(dst->includes, (dst->include_count + 1) * sizeof(char *));
            dst->includes[dst->include_count++] = src->includes[i];
        }
    }
    /* Merge ccpinclude paths (deduped) */
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

/* ─── Compile one file, recursively resolving its imports ───────────────────── */

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

    /* Walk the include list and resolve non-std paths */
    for (int i = 0; i < program->include_count; i++) {
        const char *inc = program->includes[i];

        /* std.* paths are handled by codegen (header injection), skip them */
        if (strncmp(inc, "std.", 4) == 0) continue;

        /* Resolve to a file path relative to the file's own directory */
        char *dep_path = module_to_filepath(file_dir, inc);

        /* Recursively compile the dependency */
        ProgramNode *dep = compile_file(dep_path, file_dir);
        free(dep_path);

        if (!dep) continue; /* error already printed */

        /* Merge dependency declarations BEFORE this file's own declarations
           so forward declarations come out in the right order */
        ProgramNode *merged = make_program();

        /* Copy dep's std includes */
        for (int j = 0; j < dep->include_count; j++) {
            int dup = 0;
            for (int k = 0; k < merged->include_count; k++)
                if (strcmp(merged->includes[k], dep->includes[j]) == 0) { dup = 1; break; }
            if (!dup) {
                merged->includes = realloc(merged->includes, (merged->include_count + 1) * sizeof(char *));
                merged->includes[merged->include_count++] = dep->includes[j];
            }
        }

        /* dep declarations first, then this file's */
        for (int j = 0; j < dep->decl_count; j++) {
            merged->declarations = realloc(merged->declarations, (merged->decl_count + 1) * sizeof(ASTNode *));
            merged->declarations[merged->decl_count++] = dep->declarations[j];
        }
        merge_programs(merged, program);

        program = merged;
    }

    free(file_dir);
    return program;
}

/* ─── Entry point ────────────────────────────────────────────────────────────── */

int main(int argc, char **argv) {
    const char *input_file  = NULL;
    const char *output_file = NULL;
    const char *src_dir     = ".";
    const char *target      = "linux";

    /* Parse arguments:
         hylian <input.hy>
         hylian <input.hy> -o <output.asm>
         hylian <input.hy> -o <output.asm> --src-dir <dir>
         hylian <input.hy> --target <linux|macos|windows> */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_file = argv[++i];
        } else if (strcmp(argv[i], "--src-dir") == 0 && i + 1 < argc) {
            src_dir = argv[++i];
        } else if (strcmp(argv[i], "--target") == 0 && i + 1 < argc) {
            target = argv[++i];
            if (strcmp(target, "linux") != 0 &&
                strcmp(target, "macos") != 0 &&
                strcmp(target, "windows") != 0) {
                fprintf(stderr, "hylian: unknown target '%s' (must be linux, macos, or windows)\n", target);
                return 1;
            }
        } else if (argv[i][0] != '-') {
            input_file = argv[i];
        } else {
            fprintf(stderr, "hylian: unknown option '%s'\n", argv[i]);
            fprintf(stderr, "usage: hylian <input.hy> [-o output.asm] [--src-dir dir] [--target linux|macos|windows]\n");
            return 1;
        }
    }

    /* Set default output filename if not specified */
    if (!output_file)
        output_file = "output.asm";

    if (!input_file) {
        fprintf(stderr, "hylian: no input file specified\n");
        fprintf(stderr, "usage: hylian <input.hy> [-o output.asm] [--src-dir dir] [--target linux|macos|windows]\n");
        return 1;
    }

    /* Derive src_dir from the input file's directory if not explicitly set */
    if (strcmp(src_dir, ".") == 0) {
        src_dir = dir_of(input_file);
    }

    ProgramNode *program = compile_file(input_file, src_dir);
    if (!program) return 1;

    FILE *out = fopen(output_file, "w");
    if (!out) {
        fprintf(stderr, "hylian: cannot open output file '%s'\n", output_file);
        return 1;
    }

    typecheck(program);
    codegen_asm(program, out, input_file, target);
    fclose(out);
    printf("Generated %s\n", output_file);
    return 0;
}