#ifndef LSP_ANALYSIS_H
#define LSP_ANALYSIS_H

#include "ast.h"
#include "diag.h"
#include "lsp_diag.h"


typedef enum {
    COMPLETE_VARIABLE  = 6,   /* LSP CompletionItemKind numbers */
    COMPLETE_FUNCTION  = 3,
    COMPLETE_CLASS     = 7,
    COMPLETE_FIELD     = 5,
    COMPLETE_METHOD    = 2,
    COMPLETE_KEYWORD   = 14,
    COMPLETE_MODULE    = 9,    /* LSP CompletionItemKind: Module */
} CompletionKind;

typedef struct {
    char           label[128];
    CompletionKind kind;
    char           detail[256];   /* e.g. type annotation / signature */
    char           documentation[512];
} CompletionItem;

#define MAX_COMPLETIONS 8192


typedef struct {
    int  found;
    char content[512];   /* markdown hover text */
} HoverResult;

/* One ProjectFile exists for every .hy source file the project index knows
   about. It holds the raw parse result for that file alone (not merged with
   its transitive imports — merging is done at the project level). */

#define MAX_PROJECT_FILES 256

typedef struct {
    char         filepath[1024];   /* absolute filesystem path            */
    char        *source;           /* heap copy of last known file text   */
    ProgramNode *program;          /* parsed AST (NULL on parse failure)  */
    int          dirty;            /* 1 = needs re-parse                  */

    /* Diagnostics captured during the last parse+typecheck of this file.
       Kept here so we can re-publish them without re-analysing. */
    LspDiag  diags[LSP_DIAG_MAX];
    int      diag_count;
} ProjectFile;

/* A single LspProject lives for the lifetime of the LSP session. It owns
   every ProjectFile and maintains a merged, cross-file symbol table used by
   completion and hover. */

/* One Zora project discovered under the workspace root: either the root
   itself (subdir == "") for a plain `[project]` Zora.toml, or one member of
   a `[workspace]` Zora.toml (subdir == the member name, e.g. "Corebins").
   src_dir is that project's source directory, relative to root_dir/subdir —
   taken from its target's `sources = [...]` entry, e.g. "src/main.hy" gives
   src_dir "src". Needed because a Zora workspace root (like Userland/, with
   members Init/ and Corebins/) has no single src/ of its own: module paths
   for `include { ... }` completions have to be resolved against whichever
   member sub-project a given file actually lives under. */
#define MAX_ZORA_PROJECTS 32
typedef struct {
    char subdir[256];
    char src_dir[256];
} ZoraProjectInfo;

typedef struct {
    /* Root directory of the workspace (absolute path, no trailing slash) */
    char root_dir[1024];

    /* Source directory from linkle.hy (relative to root_dir, default "src").
       Used to turn file paths into module names for include completions.
       Kept as a fallback for files that don't fall under any discovered
       Zora project (see zora_projects below). */
    char src_dir[256];

    /* Zora project(s) discovered under root_dir — see ZoraProjectInfo. */
    ZoraProjectInfo zora_projects[MAX_ZORA_PROJECTS];
    int             zora_project_count;

    /* All known source files */
    ProjectFile files[MAX_PROJECT_FILES];
    int         file_count;
CompletionItem stdlib_completions[MAX_COMPLETIONS];
int            stdlib_completion_count;
    /* Stdlib modules parsed from real .hy sources with the shared compiler
       frontend. The old path scraped .hyi interface files as TEXT, which is
       why nothing in the current pure-Hylian stdlib was visible: it ships as
       .hy, so classes like Cmd and constants like STDERR_FD were never
       indexed and the editor reported them as undefined. */
#define MAX_STDLIB_PROGRAMS 128
    ProgramNode *stdlib_programs[MAX_STDLIB_PROGRAMS];
    int          stdlib_program_count;
    /* Merged completion list built from ALL files in the project.
       Rebuilt by lsp_project_rebuild_index() whenever any file changes. */
    CompletionItem global_completions[MAX_COMPLETIONS];
    int            global_completion_count;
} LspProject;

/* One LspState is created per open document. It combines:
     - The ProjectFile for that document (parse result, per-file diagnostics)
     - A reference to the shared LspProject for cross-file lookups
 */

typedef struct {
    /* Pointer into the project's files[] array — do NOT free separately */
    ProjectFile *file;

    /* Owning project — shared across all open documents */
    LspProject  *project;

    /* Source file path (convenience copy) */
    char filepath[1024];
} LspState;

/* ═══════════════════════════════════════════════════════════════════════════
   Project API
   ═══════════════════════════════════════════════════════════════════════════ */

/*
 * lsp_project_create()
 *
 * Allocate and initialise a new LspProject for the given workspace root
 * directory (absolute path).  Scans the directory tree for all *.hy files
 * and parses them so their symbols are immediately available.
 *
 * Returns a heap-allocated LspProject; caller owns it (free with
 * lsp_project_free).  Never returns NULL.
 */
/* Diagnostic sink that records the shared frontend's parser/typechecker output
   into the LSP diagnostic buffer. Installed once at startup — the language
   server never wants anything printed to stderr. */
void lsp_diag_sink(DiagSeverity sev, const char *file, int line, int col,
                   const char *message, const char *hint, void *user);

LspProject *lsp_project_create(const char *root_dir);

void lsp_project_free(LspProject *proj);

/*
 * lsp_project_find_file()
 *
 * Look up a ProjectFile by absolute filepath.  Returns NULL if not found.
 */
ProjectFile *lsp_project_find_file(LspProject *proj, const char *filepath);

/*
 * lsp_project_get_or_add_file()
 *
 * Like lsp_project_find_file() but adds a new entry if one does not exist.
 * Returns NULL only if the project file table is full.
 */
ProjectFile *lsp_project_get_or_add_file(LspProject *proj, const char *filepath);

/*
 * lsp_project_update_file()
 *
 * Called when the editor sends new text for a file (didOpen / didChange).
 * Updates the ProjectFile's source, re-parses it, resolves its imports by
 * parsing any dependency files that are not yet in the index, then rebuilds
 * the global symbol index.
 *
 * All diagnostics for this file are captured in pf->diags / pf->diag_count.
 */
void lsp_project_update_file(LspProject *proj,
                              const char *filepath,
                              const char *source);

/*
 * lsp_project_rebuild_index()
 *
 * Walk all ProjectFiles and rebuild proj->global_completions from scratch.
 * Called automatically by lsp_project_update_file(); exposed here so callers
 * can trigger a manual rebuild if needed.
 */
void lsp_project_rebuild_index(LspProject *proj);

/* ═══════════════════════════════════════════════════════════════════════════
   Document (per-file) API
   ═══════════════════════════════════════════════════════════════════════════ */

/*
 * lsp_analyze()
 *
 * Create (or refresh) an LspState for the given file within a project.
 * If `source` is non-NULL the file's content is updated first (equivalent to
 * calling lsp_project_update_file then creating the state).
 * If `source` is NULL the existing ProjectFile content is used.
 *
 * The returned LspState is heap-allocated; free with lsp_state_free().
 * Never returns NULL.
 */
LspState *lsp_analyze(LspProject *proj,
                       const char *filepath,
                       const char *source);   /* may be NULL */

void lsp_state_free(LspState *st);

/*
 * lsp_complete()
 *
 * Fill `out` with completion items relevant at the given 0-based cursor
 * position.  Merges:
 *   - Cross-file global symbols from the project index
 *   - Local variables / parameters from the function at the cursor
 *   - Member completions when the cursor follows a '.'
 *
 * Returns the number of items written (at most max_out).
 */
int lsp_complete(LspState *st,
                 const char *source, int line, int col,
                 CompletionItem *out, int max_out);

/*
 * lsp_hover()
 *
 * Return hover information for the token under the cursor.
 * Searches the current file first, then all other project files.
 */
HoverResult lsp_hover(LspState *st,
                       const char *source, int line, int col);

/* ═══════════════════════════════════════════════════════════════════════════
   linkle.hy build-file completion
   ═══════════════════════════════════════════════════════════════════════════ */

/*
 * lsp_complete_linkle()
 *
 * Provide completions for a linkle.hy build file at the given cursor
 * position.  Context-aware: suggests block keywords at top level,
 * key names inside each block, and known values (e.g. target names)
 * after a colon.
 *
 * Returns the number of items written into `out` (at most max_out).
 */
int lsp_complete_linkle(const char *source, int line, int col,
                        CompletionItem *out, int max_out);

#endif /* LSP_ANALYSIS_H */