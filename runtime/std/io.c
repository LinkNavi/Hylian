#include <stdint.h>
#include <stdio.h>

// ── Internal read-line buffer ─────────────────────────────────────────────────

static char    _rl_buf[4096];
static int64_t _rl_pos  = 0;
static int64_t _rl_fill = 0;

// ── hylian_print ──────────────────────────────────────────────────────────────
//
// Write len bytes from str to stdout. No newline appended.

void hylian_print(char *str, int64_t len) {
    if (!str || len <= 0) return;
    fwrite(str, 1, (size_t)len, stdout);
}

// ── hylian_println ────────────────────────────────────────────────────────────
//
// Write len bytes from str to stdout, followed by a newline.

void hylian_println(char *str, int64_t len) {
    if (str && len > 0)
        fwrite(str, 1, (size_t)len, stdout);
    fputc('\n', stdout);
}

// ── hylian_int_to_str ─────────────────────────────────────────────────────────
//
// Convert n to a decimal string and write it into buf (up to buflen bytes).
// Returns the number of characters written, or 0 if buf is too small.

int64_t hylian_int_to_str(int64_t n, char *buf, int64_t buflen) {
    if (!buf || buflen <= 0) return 0;

    // Build digits in reverse in a small stack buffer.
    // INT64_MIN needs 20 digits + sign = 21 chars; 24 is safe.
    char    tmp[24];
    int     tlen = 0;
    int     neg  = 0;
    uint64_t u;

    if (n < 0) {
        neg = 1;
        // Avoid UB for INT64_MIN by going through unsigned arithmetic.
        u = (uint64_t)(-(n + 1)) + 1u;
    } else {
        u = (uint64_t)n;
    }

    if (u == 0) {
        tmp[tlen++] = '0';
    } else {
        while (u > 0) {
            tmp[tlen++] = (char)('0' + (int)(u % 10));
            u /= 10;
        }
    }

    if (neg) tmp[tlen++] = '-';

    if ((int64_t)tlen > buflen) return 0;

    // Reverse into buf.
    for (int i = 0; i < tlen; i++)
        buf[i] = tmp[tlen - 1 - i];

    return (int64_t)tlen;
}

// ── hylian_read_line ──────────────────────────────────────────────────────────
//
// Read one line from stdin into buf (up to buflen bytes). Strips trailing \n
// and \r\n. Returns the number of bytes placed in buf, or 0 on EOF.
// Uses a static 4096-byte internal buffer for efficient stdio reads.

int64_t hylian_read_line(char *buf, int64_t buflen) {
    if (!buf || buflen <= 0) return 0;

    int64_t out = 0;

    while (out < buflen) {
        // Refill the internal buffer when exhausted.
        if (_rl_pos >= _rl_fill) {
            _rl_fill = (int64_t)fread(_rl_buf, 1, sizeof(_rl_buf), stdin);
            _rl_pos  = 0;
            if (_rl_fill <= 0) break;  // EOF
        }

        char c = _rl_buf[_rl_pos++];
        if (c == '\n') goto done;
        if (c == '\r') {
            // Consume a following '\n' (Windows CRLF).
            if (_rl_pos < _rl_fill && _rl_buf[_rl_pos] == '\n')
                _rl_pos++;
            goto done;
        }
        buf[out++] = c;
    }

done:
    return out;
}

// ── hylian_str_to_int ─────────────────────────────────────────────────────────
//
// Parse a decimal integer from str[0..len). Skips leading whitespace, handles
// an optional +/- sign, and stops at the first non-digit character. Returns 0
// for empty or whitespace-only input.

int64_t hylian_str_to_int(char *str, int64_t len) {
    if (!str || len <= 0) return 0;

    int64_t i = 0;

    // Skip leading whitespace.
    while (i < len && (str[i] == ' ' || str[i] == '\t' ||
                       str[i] == '\n' || str[i] == '\r'))
        i++;

    if (i >= len) return 0;

    int64_t sign = 1;
    if      (str[i] == '-') { sign = -1; i++; }
    else if (str[i] == '+') {            i++; }

    int64_t result = 0;
    while (i < len && str[i] >= '0' && str[i] <= '9') {
        result = result * 10 + (int64_t)(str[i] - '0');
        i++;
    }

    return sign * result;
}
