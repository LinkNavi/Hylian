#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#  include <direct.h>
#  define HYLIAN_MKDIR(path)  _mkdir(path)
#  define HYLIAN_GETCWD(b,n)  _getcwd((b),(int)(n))
#  define HYLIAN_SEP          '\\'
#else
#  include <unistd.h>
#  define HYLIAN_MKDIR(path)  mkdir((path), 0755)
#  define HYLIAN_GETCWD(b,n)  getcwd((b),(size_t)(n))
#  define HYLIAN_SEP          '/'
#endif

// ── hylian_file_read ──────────────────────────────────────────────────────────
//
// Read the entire contents of the file at `path` into buf (up to buf_len
// bytes). Returns the number of bytes read, or -1 on error.

int64_t hylian_file_read(char *path, int64_t path_len, char *buf, int64_t buf_len) {
    if (!path || path_len <= 0 || path_len > 4096 || !buf || buf_len <= 0) return -1;
    char tmp[4097];
    memcpy(tmp, path, (size_t)path_len);
    tmp[path_len] = '\0';

    FILE *f = fopen(tmp, "rb");
    if (!f) return -1;
    int64_t n = (int64_t)fread(buf, 1, (size_t)buf_len, f);
    fclose(f);
    return n;
}

// ── hylian_file_write ─────────────────────────────────────────────────────────
//
// Write buf to the file at `path`, creating or truncating it. Returns the
// number of bytes written, or -1 on error.

int64_t hylian_file_write(char *path, int64_t path_len, char *buf, int64_t buf_len) {
    if (!path || path_len <= 0 || path_len > 4096 || !buf || buf_len <= 0) return -1;
    char tmp[4097];
    memcpy(tmp, path, (size_t)path_len);
    tmp[path_len] = '\0';

    FILE *f = fopen(tmp, "wb");
    if (!f) return -1;
    int64_t n = (int64_t)fwrite(buf, 1, (size_t)buf_len, f);
    fclose(f);
    return n;
}

// ── hylian_file_append ────────────────────────────────────────────────────────
//
// Append buf to the file at `path`, creating it if it doesn't exist. Returns
// the number of bytes written, or -1 on error.

int64_t hylian_file_append(char *path, int64_t path_len, char *buf, int64_t buf_len) {
    if (!path || path_len <= 0 || path_len > 4096 || !buf || buf_len <= 0) return -1;
    char tmp[4097];
    memcpy(tmp, path, (size_t)path_len);
    tmp[path_len] = '\0';

    FILE *f = fopen(tmp, "ab");
    if (!f) return -1;
    int64_t n = (int64_t)fwrite(buf, 1, (size_t)buf_len, f);
    fclose(f);
    return n;
}

// ── hylian_file_exists ────────────────────────────────────────────────────────
//
// Returns 1 if a file exists at `path`, 0 if it does not.

int64_t hylian_file_exists(char *path, int64_t path_len) {
    if (!path || path_len <= 0 || path_len > 4096) return 0;
    char tmp[4097];
    memcpy(tmp, path, (size_t)path_len);
    tmp[path_len] = '\0';

    FILE *f = fopen(tmp, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

// ── hylian_file_size ──────────────────────────────────────────────────────────
//
// Returns the size of the file at `path` in bytes, or -1 on error.

int64_t hylian_file_size(char *path, int64_t path_len) {
    if (!path || path_len <= 0 || path_len > 4096) return -1;
    char tmp[4097];
    memcpy(tmp, path, (size_t)path_len);
    tmp[path_len] = '\0';

    FILE *f = fopen(tmp, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    int64_t size = (int64_t)ftell(f);
    fclose(f);
    return size;
}

// ── hylian_getcwd ─────────────────────────────────────────────────────────────
//
// Write the current working directory path into buf (up to buf_len bytes).
// Returns the number of bytes written (not including NUL), or -1 on error.

int64_t hylian_getcwd(char *buf, int64_t buf_len) {
    if (!buf || buf_len <= 0) return -1;
    if (!HYLIAN_GETCWD(buf, buf_len)) return -1;
    return (int64_t)strlen(buf);
}

// ── hylian_parent_dir ─────────────────────────────────────────────────────────
//
// Write the parent directory of `path` into buf (up to buf_len bytes).
// Strips the last path component. Returns the number of bytes written, or -1.
// If `path` is already a root (e.g. "/" or "C:\"), copies it unchanged.

int64_t hylian_parent_dir(char *path, int64_t path_len,
                           char *buf,  int64_t buf_len) {
    if (!path || path_len <= 0 || path_len > 4096 || !buf || buf_len <= 0)
        return -1;

    char tmp[4097];
    memcpy(tmp, path, (size_t)path_len);
    tmp[path_len] = '\0';

    /* Find last separator */
    char *last = NULL;
    for (char *p = tmp; *p; p++)
        if (*p == '/' || *p == '\\') last = p;

    if (!last) {
        /* No separator — return "." */
        if (buf_len < 2) return -1;
        buf[0] = '.'; buf[1] = '\0';
        return 1;
    }

    /* Root edge-case: last separator IS the root slash */
    if (last == tmp) {
        if (buf_len < 2) return -1;
        buf[0] = '/'; buf[1] = '\0';
        return 1;
    }

    int64_t len = (int64_t)(last - tmp);
    if (len >= buf_len) return -1;
    memcpy(buf, tmp, (size_t)len);
    buf[len] = '\0';
    return len;
}

// ── hylian_mkdir ──────────────────────────────────────────────────────────────
//
// Create a directory at `path` (mode 0755 on POSIX, default on Windows).
// Returns 0 on success, -1 on failure (e.g. already exists, permission denied).

int64_t hylian_mkdir(char *path, int64_t path_len) {
    if (!path || path_len <= 0 || path_len > 4096) return -1;
    char tmp[4097];
    memcpy(tmp, path, (size_t)path_len);
    tmp[path_len] = '\0';

    return (HYLIAN_MKDIR(tmp) == 0) ? 0 : -1;
}
