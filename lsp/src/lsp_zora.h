#ifndef LSP_ZORA_H
#define LSP_ZORA_H

/* Zora.toml discovery. The project a file belongs to is decided by walking UP
   from the file to the nearest Zora.toml - not by whatever folder the editor
   happened to open - so a workspace opened at a repo root still resolves each
   file against its own member project (its own src/, vendors/ and deps). */

#define ZORA_MAX_DEPS 16

typedef struct {
    int  found;              /* a Zora.toml was found above the file          */
    char root[1024];         /* directory of the nearest Zora.toml            */
    char workspace[1024];    /* outermost enclosing [workspace] root, or root */
    char name[128];          /* [project] name                                */
    char target[128];        /* the target whose sources contain the file     */
    char entry[1024];        /* absolute path of that target's entry file     */
    char src_dir[1024];      /* directory of the entry: the include root      */
    char vendors_dir[1100];  /* <root>/vendors                                */
    int  dep_count;          /* `system = "..."` deps of the target           */
    char deps[ZORA_MAX_DEPS][128];
    char toml_path[1100];
} ZoraInfo;

/* Fill `out` for `filepath` (absolute). Always succeeds: with no Zora.toml the
   file's own directory becomes root and src_dir. */
void zora_info_for_file(const char *filepath, ZoraInfo *out);

/* Drop cached Zora.toml parses (call when a Zora.toml changes). */
void zora_invalidate(void);

#endif
