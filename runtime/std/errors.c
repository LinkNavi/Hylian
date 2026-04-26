#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ── hylian_make_error ─────────────────────────────────────────────────────────
//
// Allocate an Error struct (16 bytes):
//   offset  0: char*   message  — heap copy of msg, null-terminated
//   offset  8: int64_t code     — always 0 (reserved for future use)
//
// Returns a pointer to the struct, or NULL on OOM.

void *hylian_make_error(char *msg, int64_t len) {
    if (!msg || len < 0) return NULL;

    // Allocate a null-terminated copy of the message.
    char *message = (char *)malloc((size_t)(len + 1));
    if (!message) return NULL;
    memcpy(message, msg, (size_t)len);
    message[len] = '\0';

    // Allocate the Error struct (16 bytes: one pointer + one int64_t).
    char **err = (char **)malloc(16);
    if (!err) {
        free(message);
        return NULL;
    }

    err[0] = message;                          // offset 0: char* message
    *((int64_t *)(err + 1)) = (int64_t)0;      // offset 8: int64_t code = 0

    return (void *)err;
}

// ── hylian_panic ─────────────────────────────────────────────────────────────
//
// Write "panic: <msg>\n" to stderr and exit with code 1. Does not return.

void hylian_panic(char *msg, int64_t len) {
    const char prefix[] = "panic: ";
    fwrite(prefix, 1, sizeof(prefix) - 1, stderr);
    if (msg && len > 0)
        fwrite(msg, 1, (size_t)len, stderr);
    fputc('\n', stderr);
    exit(1);
}
