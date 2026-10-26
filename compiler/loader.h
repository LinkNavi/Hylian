#ifndef LOADER_H
#define LOADER_H

#include "ast.h"

/* Path helpers (all return heap strings the caller frees). */
char *dir_of(const char *path);
char *module_to_filepath(const char *src_dir, const char *module);
char *resolve_dotted_include(const char *dir, const char *module);
char *trim(char *s);

/* Parse a vendor/std interface file (.hyi) into a synthetic program. The first
   `link` library goes to *out_link, `pkg` names to out_pkgs (caller frees). */
ProgramNode *parse_hyi_into_program(const char *filepath, char **out_link,
                                    char ***out_pkgs, int *out_pkg_count);

typedef enum {
    INC_FILE,    /* a project module: src_dir/<dotted path>.hy               */
    INC_STD,     /* std.<name>: interface and/or source in the stdlib tree   */
    INC_VENDOR,  /* vendors.<name>: <project>/vendors/<name>/<name>.hyi      */
    INC_SKIP,    /* link:/pkg: hints and the `kernel` pseudo-module - no file */
} IncKind;

typedef struct {
    IncKind kind;
    char *hyi;      /* interface file that exists, or NULL                    */
    char *hy;       /* source file (for INC_FILE it may not exist: the path
                       the compiler reports in its "cannot open" error)        */
    char *hy_root;  /* INC_STD: the stdlib root to compile `hy` against       */
} ResolvedInclude;

/* Resolve one include path exactly as the compiler does. `src_dir` is the
   directory of the entry file (the project root is its parent); `file_dir`
   the directory of the file containing the include. */
void loader_resolve_include(const char *inc, const char *src_dir, const char *file_dir,
                            ResolvedInclude *out);
void loader_free_resolved(ResolvedInclude *r);

/* The installed/selected stdlib directory: $HYLIAN_LIB[/std], else
   /usr/local/lib/hylian/std. */
void loader_installed_std_dir(char *buf, size_t bufsz);

#endif
