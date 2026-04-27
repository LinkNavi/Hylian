#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// ── hylian_getenv ─────────────────────────────────────────────────────────────
//
// Look up the environment variable whose name is the first name_len bytes of
// `name` (NOT null-terminated). Copies the value into buf (up to buf_len
// bytes) and returns the number of bytes copied. Returns -1 if the variable
// is not found, or if any argument is invalid.

int64_t hylian_getenv(char *name, int64_t name_len, char *buf, int64_t buf_len) {
    if (!name || name_len <= 0 || name_len > 4096 || !buf || buf_len <= 0)
        return -1;

    // Build a null-terminated copy of the name on the stack.
    char tmp[4097];
    memcpy(tmp, name, (size_t)name_len);
    tmp[name_len] = '\0';

    const char *value = getenv(tmp);
    if (!value) return -1;

    int64_t vlen = (int64_t)strlen(value);
    int64_t copy = vlen < buf_len ? vlen : buf_len;
    memcpy(buf, value, (size_t)copy);
    return copy;
}

// ── hylian_exit ───────────────────────────────────────────────────────────────
//
// Terminate the process with the given exit code. Does not return.

void hylian_exit(int64_t code) {
    exit((int)code);
}

// ── hylian_exec ───────────────────────────────────────────────────────────────
//
// Run `cmd` in the system shell (cmd.exe on Windows, /bin/sh elsewhere).
// Returns the command's exit code, or -1 if the shell could not be launched.
// Uses system() which is available on all supported platforms.

int64_t hylian_exec(char *cmd, int64_t cmd_len) {
    if (!cmd || cmd_len <= 0 || cmd_len > 65536) return -1;
    char tmp[65537];
    memcpy(tmp, cmd, (size_t)cmd_len);
    tmp[cmd_len] = '\0';
    int ret = system(tmp);
#ifdef _WIN32
    return (int64_t)ret;
#else
    /* On POSIX, system() returns a wait-status; extract the exit code */
    if (ret == -1) return -1;
    return (int64_t)((ret >> 8) & 0xff);
#endif
}

// ── hylian_os ─────────────────────────────────────────────────────────────────
//
// Write the current OS name into buf (up to buf_len bytes).
// Returns the number of bytes written, or -1 on error.
// Possible values: "windows", "macos", "linux", "unknown"

int64_t hylian_os(char *buf, int64_t buf_len) {
    if (!buf || buf_len <= 0) return -1;
#if defined(_WIN32)
    static const char name[] = "windows";
#elif defined(__APPLE__)
    static const char name[] = "macos";
#elif defined(__linux__)
    static const char name[] = "linux";
#else
    static const char name[] = "unknown";
#endif
    int64_t len  = (int64_t)strlen(name);
    int64_t copy = len < buf_len ? len : buf_len - 1;
    memcpy(buf, name, (size_t)copy);
    buf[copy] = '\0';
    return copy;
}
