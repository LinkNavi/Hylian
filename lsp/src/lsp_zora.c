#include "lsp_zora.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

/* ── a small TOML reader: just the subset Zora.toml uses ─────────────────── */

#define MAX_TARGETS 16
#define MAX_SOURCES 8

typedef struct {
    char name[128];
    char sources[MAX_SOURCES][256];
    int  source_count;
    char deps[ZORA_MAX_DEPS][128];
    int  dep_count;
} TomlTarget;

typedef struct {
    char        path[1100];
    time_t      mtime;
    char        name[128];
    int         is_workspace;
    TomlTarget  targets[MAX_TARGETS];
    int         target_count;
} TomlFile;

#define CACHE_MAX 32
static TomlFile g_cache[CACHE_MAX];
static int g_cache_count = 0;

void zora_invalidate(void) { g_cache_count = 0; }

static char *strip(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\n' || e[-1] == '\r')) *--e = '\0';
    return s;
}

/* first "quoted" string in `s` */
static int first_quoted(const char *s, char *out, size_t n) {
    const char *a = strchr(s, '"');
    if (!a) return 0;
    const char *b = strchr(a + 1, '"');
    if (!b) return 0;
    size_t len = (size_t)(b - a - 1);
    if (len >= n) len = n - 1;
    memcpy(out, a + 1, len);
    out[len] = '\0';
    return 1;
}

static void parse_toml(TomlFile *tf) {
    FILE *f = fopen(tf->path, "r");
    if (!f) return;
    enum { S_NONE, S_PROJECT, S_WORKSPACE, S_TARGET, S_DEP, S_OTHER } sect = S_NONE;
    TomlTarget *cur = NULL;
    char line[1024];
    int in_sources = 0;
    while (fgets(line, sizeof(line), f)) {
        char *s = strip(line);
        if (!*s || *s == '#') continue;

        if (*s == '[') {
            in_sources = 0;
            int dbl = s[1] == '[';
            char *name = s + (dbl ? 2 : 1);
            char *end = strchr(name, ']');
            if (end) *end = '\0';
            name = strip(name);
            if (!strcmp(name, "project")) sect = S_PROJECT;
            else if (!strcmp(name, "workspace")) { sect = S_WORKSPACE; tf->is_workspace = 1; }
            else if (!strncmp(name, "target.", 7)) {
                char tname[128];
                snprintf(tname, sizeof(tname), "%s", name + 7);
                int is_dep = 0;
                char *dot = strstr(tname, ".deps");
                if (dot) { *dot = '\0'; is_dep = 1; }
                cur = NULL;
                for (int i = 0; i < tf->target_count; i++)
                    if (!strcmp(tf->targets[i].name, tname)) cur = &tf->targets[i];
                if (!cur && tf->target_count < MAX_TARGETS) {
                    cur = &tf->targets[tf->target_count++];
                    memset(cur, 0, sizeof(*cur));
                    snprintf(cur->name, sizeof(cur->name), "%s", tname);
                }
                sect = is_dep ? S_DEP : S_TARGET;
            } else sect = S_OTHER;
            continue;
        }

        /* a multi-line `sources = [` continues until `]` */
        if (in_sources && cur) {
            char buf[256];
            const char *p = s;
            while ((p = strchr(p, '"')) != NULL && cur->source_count < MAX_SOURCES) {
                if (!first_quoted(p, buf, sizeof(buf))) break;
                snprintf(cur->sources[cur->source_count++], 256, "%s", buf);
                p = strchr(p + 1, '"');
                if (!p) break;
                p++;
            }
            if (strchr(s, ']')) in_sources = 0;
            continue;
        }

        char *eq = strchr(s, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = strip(s), *val = strip(eq + 1);
        char buf[256];

        if (sect == S_PROJECT && !strcmp(key, "name") && first_quoted(val, buf, sizeof(buf)))
            snprintf(tf->name, sizeof(tf->name), "%s", buf);
        else if (sect == S_TARGET && cur && !strcmp(key, "sources")) {
            const char *p = val;
            while ((p = strchr(p, '"')) != NULL && cur->source_count < MAX_SOURCES) {
                if (!first_quoted(p, buf, sizeof(buf))) break;
                snprintf(cur->sources[cur->source_count++], 256, "%s", buf);
                p = strchr(p + 1, '"');
                if (!p) break;
                p++;
            }
            if (strchr(val, '[') && !strchr(val, ']')) in_sources = 1;
        } else if (sect == S_DEP && cur && !strcmp(key, "system") &&
                   first_quoted(val, buf, sizeof(buf)) && cur->dep_count < ZORA_MAX_DEPS) {
            snprintf(cur->deps[cur->dep_count++], 128, "%s", buf);
        }
    }
    fclose(f);
}

static TomlFile *load_toml(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return NULL;
    for (int i = 0; i < g_cache_count; i++)
        if (!strcmp(g_cache[i].path, path)) {
            if (g_cache[i].mtime == st.st_mtime) return &g_cache[i];
            memset(&g_cache[i], 0, sizeof(TomlFile));
            snprintf(g_cache[i].path, sizeof(g_cache[i].path), "%s", path);
            g_cache[i].mtime = st.st_mtime;
            parse_toml(&g_cache[i]);
            return &g_cache[i];
        }
    if (g_cache_count >= CACHE_MAX) g_cache_count = 0;
    TomlFile *tf = &g_cache[g_cache_count++];
    memset(tf, 0, sizeof(*tf));
    snprintf(tf->path, sizeof(tf->path), "%s", path);
    tf->mtime = st.st_mtime;
    parse_toml(tf);
    return tf;
}

/* ── discovery ───────────────────────────────────────────────────────────── */

static int is_file(const char *p) {
    struct stat st;
    return stat(p, &st) == 0 && S_ISREG(st.st_mode);
}

static int is_dir(const char *p) {
    struct stat st;
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

static void dir_of_path(const char *path, char *out, size_t n) {
    snprintf(out, n, "%s", path);
    char *slash = strrchr(out, '/');
    if (slash && slash != out) *slash = '\0';
    else if (slash) out[1] = '\0';
    else snprintf(out, n, ".");
}

/* directory part of a source pattern such as "src/main.hy" or a glob like "src/<any>/<name>.hy" */
static void pattern_dir(const char *pattern, char *out, size_t n) {
    snprintf(out, n, "%s", pattern);
    char *glob = strpbrk(out, "*?[");
    if (glob) *glob = '\0';
    char *slash = strrchr(out, '/');
    if (slash) *slash = '\0'; else out[0] = '\0';
}

static int path_under(const char *file, const char *dir) {
    size_t dl = strlen(dir);
    return strncmp(file, dir, dl) == 0 && (file[dl] == '/' || file[dl] == '\0');
}

void zora_info_for_file(const char *filepath, ZoraInfo *out) {
    memset(out, 0, sizeof(*out));

    char dir[1024];
    dir_of_path(filepath, dir, sizeof(dir));

    /* nearest Zora.toml walking up; remember the outermost [workspace] one */
    char nearest[1024] = "", workspace[1024] = "";
    char cur[1024];
    snprintf(cur, sizeof(cur), "%s", dir);
    for (int depth = 0; depth < 64; depth++) {
        char toml[1100];
        snprintf(toml, sizeof(toml), "%s/Zora.toml", cur[1] ? cur : "");
        if (is_file(toml)) {
            if (!nearest[0]) snprintf(nearest, sizeof(nearest), "%s", cur);
            TomlFile *tf = load_toml(toml);
            if (tf && tf->is_workspace) snprintf(workspace, sizeof(workspace), "%s", cur);
        }
        if (!strcmp(cur, "/") || !cur[0]) break;
        char parent[1024];
        dir_of_path(cur, parent, sizeof(parent));
        if (!strcmp(parent, cur)) break;
        snprintf(cur, sizeof(cur), "%s", parent);
    }

    if (!nearest[0]) {
        /* no Zora.toml anywhere above: treat the file's folder as the project */
        out->found = 0;
        snprintf(out->root, sizeof(out->root), "%s", dir);
        snprintf(out->workspace, sizeof(out->workspace), "%s", dir);
        snprintf(out->src_dir, sizeof(out->src_dir), "%s", dir);
        snprintf(out->vendors_dir, sizeof(out->vendors_dir), "%s/vendors", dir);
        return;
    }

    out->found = 1;
    snprintf(out->root, sizeof(out->root), "%s", nearest);
    snprintf(out->workspace, sizeof(out->workspace), "%s", workspace[0] ? workspace : nearest);
    snprintf(out->toml_path, sizeof(out->toml_path), "%s/Zora.toml", nearest);
    snprintf(out->vendors_dir, sizeof(out->vendors_dir), "%s/vendors", nearest);

    TomlFile *tf = load_toml(out->toml_path);
    if (tf) snprintf(out->name, sizeof(out->name), "%s", tf->name);

    /* the target whose sources' directory contains the file, else the first
       target that has a source at all */
    TomlTarget *pick = NULL;
    char pick_entry[1024] = "";
    if (tf) {
        for (int t = 0; t < tf->target_count && !pick; t++) {
            for (int s = 0; s < tf->targets[t].source_count; s++) {
                char pd[256], abs_dir[1400];
                pattern_dir(tf->targets[t].sources[s], pd, sizeof(pd));
                snprintf(abs_dir, sizeof(abs_dir), "%s%s%s", nearest, pd[0] ? "/" : "", pd);
                if (path_under(filepath, abs_dir)) {
                    pick = &tf->targets[t];
                    snprintf(pick_entry, sizeof(pick_entry), "%s/%s", nearest, tf->targets[t].sources[s]);
                    break;
                }
            }
        }
        for (int t = 0; t < tf->target_count && !pick; t++)
            if (tf->targets[t].source_count > 0) {
                pick = &tf->targets[t];
                snprintf(pick_entry, sizeof(pick_entry), "%s/%s", nearest, pick->sources[0]);
            }
    }

    if (pick) {
        snprintf(out->target, sizeof(out->target), "%s", pick->name);
        if (!strpbrk(pick_entry, "*?[")) snprintf(out->entry, sizeof(out->entry), "%s", pick_entry);
        char pd[256];
        pattern_dir(strstr(pick_entry, nearest) == pick_entry ? pick_entry + strlen(nearest) + 1 : pick_entry,
                    pd, sizeof(pd));
        snprintf(out->src_dir, sizeof(out->src_dir), "%s%s%s", nearest, pd[0] ? "/" : "", pd);
        for (int i = 0; i < pick->dep_count; i++)
            snprintf(out->deps[out->dep_count++], 128, "%s", pick->deps[i]);
    } else {
        char guess[1100];
        snprintf(guess, sizeof(guess), "%s/src", nearest);
        snprintf(out->src_dir, sizeof(out->src_dir), "%s", is_dir(guess) ? guess : nearest);
    }
}
