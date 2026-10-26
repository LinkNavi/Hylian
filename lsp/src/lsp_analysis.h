#ifndef LSP_ANALYSIS_H
#define LSP_ANALYSIS_H

/* Language-server analysis for Hylian, built on the compiler's own frontend
 * (lexer, parser, typechecker and include resolver), so the editor and the
 * compiler cannot disagree about what a program means.
 *
 *   project   - session state: module cache, unsaved-buffer overlay
 *   state     - one analysed file: its AST, the modules it can see, diagnostics
 *   features  - completion, hover, definition, signature help, symbols
 */

#include "ast.h"
#include "lsp_diag.h"
#include "lsp_zora.h"

typedef enum {
    COMPLETE_TEXT        = 1,
    COMPLETE_METHOD      = 2,
    COMPLETE_FUNCTION    = 3,
    COMPLETE_CONSTRUCTOR = 4,
    COMPLETE_FIELD       = 5,
    COMPLETE_VARIABLE    = 6,
    COMPLETE_CLASS       = 7,
    COMPLETE_MODULE      = 9,
    COMPLETE_PROPERTY    = 10,
    COMPLETE_ENUM        = 13,
    COMPLETE_KEYWORD     = 14,
    COMPLETE_SNIPPET     = 15,
    COMPLETE_ENUM_MEMBER = 20,
    COMPLETE_CONSTANT    = 21,
    COMPLETE_STRUCT      = 22,
} CompletionKind;

typedef struct {
    char           label[128];
    CompletionKind kind;
    char           detail[256];         /* signature / type                    */
    char           documentation[512];  /* markdown                            */
    char           insert_text[256];    /* "" = insert the label               */
    int            is_snippet;          /* insert_text uses $1 / $0 tab stops  */
    char           sort_key[16];        /* orders groups: locals first, ...    */
    /* auto-import: also add `import_text` at (import_line, import_col) */
    int            has_import;
    int            import_line, import_col;
    char           import_text[192];
} CompletionItem;

typedef struct {
    int  found;
    char content[1024];   /* markdown */
} HoverResult;

typedef struct {
    int  found;
    char path[1024];
    int  line, col;           /* 0-based start of the name */
    int  end_col;
} DefLocation;

#define SIG_MAX_PARAMS 16
typedef struct {
    int  found;
    char label[384];                       /* full signature                 */
    char doc[512];
    int  param_count;
    int  param_start[SIG_MAX_PARAMS];      /* byte offsets of each param in label */
    int  param_end[SIG_MAX_PARAMS];
    int  active;                           /* index of the argument at the cursor */
} SignatureInfo;

typedef struct {
    char name[128];
    char detail[128];
    int  kind;                             /* LSP SymbolKind                 */
    int  line, col, end_line, end_col;     /* full range                     */
    int  name_col;                         /* selection: name on `line`      */
    int  parent;                           /* index of the parent, -1 = top  */
} SymbolInfo;

typedef struct LspProject LspProject;
typedef struct LspState   LspState;

/* ── project ─────────────────────────────────────────────────────────────── */

LspProject *lsp_project_create(const char *workspace_root);
void        lsp_project_free(LspProject *proj);

/* The editor's unsaved text for `path` (NULL text = the buffer was closed).
   Other files that include `path` see this text instead of the disk copy. */
void lsp_project_set_overlay(LspProject *proj, const char *path, const char *text);

/* A file changed on disk (or a Zora.toml did): drop what was cached for it. */
void lsp_project_file_changed(LspProject *proj, const char *path);

/* ── one file ────────────────────────────────────────────────────────────── */

/* Parse, resolve includes, typecheck. Never returns NULL. */
LspState *lsp_analyze(LspProject *proj, const char *path, const char *text);
void      lsp_state_free(LspState *st);

const LspDiag *lsp_state_diags(const LspState *st, int *count);
const ZoraInfo *lsp_state_zora(const LspState *st);

/* ── features (0-based line / column) ────────────────────────────────────── */

/* Returns a malloc'd array (caller frees) of relevant items, or NULL. */
CompletionItem *lsp_complete(LspState *st, int line, int col, int *count, int *incomplete);
HoverResult     lsp_hover(LspState *st, int line, int col);
DefLocation     lsp_definition(LspState *st, int line, int col);
SignatureInfo   lsp_signature_help(LspState *st, int line, int col);
SymbolInfo     *lsp_document_symbols(LspState *st, int *count);   /* malloc'd */

#endif /* LSP_ANALYSIS_H */
