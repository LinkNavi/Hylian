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
