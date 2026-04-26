#include "lsp_proto.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ─── LSP message framing ───────────────────────────────────────────────────── */

char *lsp_read_message(FILE *in) {
    long content_length = -1;
    char header_buf[256];

    /* Read headers line by line until we hit the blank \r\n separator */
    while (1) {
        if (!fgets(header_buf, sizeof(header_buf), in)) return NULL;

        /* Strip trailing \r\n */
        int len = (int)strlen(header_buf);
        while (len > 0 && (header_buf[len-1] == '\r' || header_buf[len-1] == '\n'))
            header_buf[--len] = '\0';

        /* Blank line = end of headers */
        if (len == 0) break;

        /* Parse Content-Length */
        if (strncmp(header_buf, "Content-Length:", 15) == 0) {
            content_length = atol(header_buf + 15);
        }
        /* Ignore other headers (Content-Type, etc.) */
    }

    if (content_length <= 0) return NULL;

    char *body = malloc(content_length + 1);
    if (!body) return NULL;

    size_t got = fread(body, 1, content_length, in);
    if ((long)got != content_length) {
        free(body);
        return NULL;
    }
    body[content_length] = '\0';
    return body;
}

void lsp_write_message(FILE *out, const char *json) {
    int len = (int)strlen(json);
    fprintf(out, "Content-Length: %d\r\n\r\n", len);
    fwrite(json, 1, len, out);
    fflush(out);
}

/* ─── JSON builder ──────────────────────────────────────────────────────────── */

static void jb_ensure(JsonBuf *b, size_t extra) {
    if (b->len + extra + 1 > b->cap) {
        b->cap = (b->cap == 0) ? 4096 : b->cap * 2;
        while (b->len + extra + 1 > b->cap) b->cap *= 2;
        b->buf = realloc(b->buf, b->cap);
    }
}

void jb_init(JsonBuf *b) {
    b->buf = NULL;
    b->len = 0;
    b->cap = 0;
    b->depth = 0;
    memset(b->need_comma, 0, sizeof(b->need_comma));
}

void jb_free(JsonBuf *b) {
    free(b->buf);
    b->buf = NULL;
    b->len = 0;
    b->cap = 0;
}

void jb_raw(JsonBuf *b, const char *s) {
    size_t slen = strlen(s);
    jb_ensure(b, slen);
    memcpy(b->buf + b->len, s, slen);
    b->len += slen;
    b->buf[b->len] = '\0';
}

/* Emit a JSON-escaped string value (with surrounding quotes) */
static void jb_quoted(JsonBuf *b, const char *s) {
    jb_ensure(b, strlen(s) * 6 + 4);
    b->buf[b->len++] = '"';
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"')       { b->buf[b->len++] = '\\'; b->buf[b->len++] = '"';  }
        else if (c == '\\') { b->buf[b->len++] = '\\'; b->buf[b->len++] = '\\'; }
        else if (c == '\n') { b->buf[b->len++] = '\\'; b->buf[b->len++] = 'n';  }
        else if (c == '\r') { b->buf[b->len++] = '\\'; b->buf[b->len++] = 'r';  }
        else if (c == '\t') { b->buf[b->len++] = '\\'; b->buf[b->len++] = 't';  }
        else if (c < 0x20) {
            /* control character — emit \uXXXX */
            b->len += sprintf(b->buf + b->len, "\\u%04x", c);
        } else {
            b->buf[b->len++] = (char)c;
        }
    }
    b->buf[b->len++] = '"';
    b->buf[b->len]   = '\0';
}

/* Emit a comma if needed for the current depth, then mark that subsequent
   items at this depth will need a comma */
static void jb_maybe_comma(JsonBuf *b) {
    if (b->depth > 0 && b->need_comma[b->depth - 1]) {
        jb_raw(b, ",");
    }
    if (b->depth > 0)
        b->need_comma[b->depth - 1] = 1;
}

void jb_obj_begin(JsonBuf *b) {
    jb_maybe_comma(b);
    jb_raw(b, "{");
    if (b->depth < 63) {
        b->need_comma[b->depth] = 0;
        b->depth++;
    }
}

void jb_obj_end(JsonBuf *b) {
    if (b->depth > 0) b->depth--;
    jb_raw(b, "}");
    if (b->depth > 0) b->need_comma[b->depth - 1] = 1;
}

void jb_arr_begin(JsonBuf *b) {
    jb_maybe_comma(b);
    jb_raw(b, "[");
    if (b->depth < 63) {
        b->need_comma[b->depth] = 0;
        b->depth++;
    }
}

void jb_arr_end(JsonBuf *b) {
    if (b->depth > 0) b->depth--;
    jb_raw(b, "]");
    if (b->depth > 0) b->need_comma[b->depth - 1] = 1;
}

void jb_key(JsonBuf *b, const char *key) {
    jb_maybe_comma(b);
    /* After emitting a key we reset the comma flag for this level so the
       value that follows doesn't get a spurious comma. The key itself
       consumed the comma slot. */
    if (b->depth > 0) b->need_comma[b->depth - 1] = 0;
    jb_quoted(b, key);
    jb_raw(b, ":");
}

void jb_key_str(JsonBuf *b, const char *key, const char *val) {
    jb_key(b, key);
    jb_quoted(b, val ? val : "");
    if (b->depth > 0) b->need_comma[b->depth - 1] = 1;
}

void jb_key_int(JsonBuf *b, const char *key, long val) {
    jb_key(b, key);
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "%ld", val);
    jb_raw(b, tmp);
    if (b->depth > 0) b->need_comma[b->depth - 1] = 1;
}

void jb_key_bool(JsonBuf *b, const char *key, int val) {
    jb_key(b, key);
    jb_raw(b, val ? "true" : "false");
    if (b->depth > 0) b->need_comma[b->depth - 1] = 1;
}

void jb_key_null(JsonBuf *b, const char *key) {
    jb_key(b, key);
    jb_raw(b, "null");
    if (b->depth > 0) b->need_comma[b->depth - 1] = 1;
}

void jb_key_begin_obj(JsonBuf *b, const char *key) {
    jb_key(b, key);
    jb_raw(b, "{");
    if (b->depth < 63) {
        b->need_comma[b->depth] = 0;
        b->depth++;
    }
}

void jb_key_begin_arr(JsonBuf *b, const char *key) {
    jb_key(b, key);
    jb_raw(b, "[");
    if (b->depth < 63) {
        b->need_comma[b->depth] = 0;
        b->depth++;
    }
}

void jb_arr_str(JsonBuf *b, const char *val) {
    jb_maybe_comma(b);
    jb_quoted(b, val ? val : "");
    if (b->depth > 0) b->need_comma[b->depth - 1] = 1;
}

void jb_arr_int(JsonBuf *b, long val) {
    jb_maybe_comma(b);
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "%ld", val);
    jb_raw(b, tmp);
    if (b->depth > 0) b->need_comma[b->depth - 1] = 1;
}

void jb_arr_begin_obj(JsonBuf *b) {
    jb_obj_begin(b);
}

/* ─── JSON reader helpers ───────────────────────────────────────────────────── */

/* Skip whitespace */
static const char *skip_ws(const char *p) {
    while (*p && isspace((unsigned char)*p)) p++;
    return p;
}

/* Skip past a complete JSON value (string, number, object, array, literal).
   Returns pointer just past the value, or NULL on error. */
static const char *skip_value(const char *p) {
    p = skip_ws(p);
    if (!*p) return NULL;

    if (*p == '"') {
        p++; /* opening quote */
        while (*p && *p != '"') {
            if (*p == '\\') p++; /* skip escaped char */
            if (*p) p++;
        }
        if (*p == '"') p++;
        return p;
    }
    if (*p == '{') {
        p++;
        int depth = 1;
        while (*p && depth > 0) {
            if (*p == '"') { p = skip_value(p); if (!p) return NULL; continue; }
            if (*p == '{') depth++;
            else if (*p == '}') depth--;
            if (depth > 0) p++;
            else p++;
        }
        return p;
    }
    if (*p == '[') {
        p++;
        int depth = 1;
        while (*p && depth > 0) {
            if (*p == '"') { p = skip_value(p); if (!p) return NULL; continue; }
            if (*p == '[') depth++;
            else if (*p == ']') depth--;
            if (depth > 0) p++;
            else p++;
        }
        return p;
    }
    /* number / literal */
    while (*p && *p != ',' && *p != '}' && *p != ']' && !isspace((unsigned char)*p))
        p++;
    return p;
}

/* Unquote a JSON string value into out buffer.
   `p` should point at the opening '"'. */
static int unquote_str(const char *p, char *out, int outsz) {
    p = skip_ws(p);
    if (*p != '"') return 0;
    p++;
    int i = 0;
    while (*p && *p != '"' && i < outsz - 1) {
        if (*p == '\\') {
            p++;
            switch (*p) {
            case '"':  out[i++] = '"';  break;
            case '\\': out[i++] = '\\'; break;
            case '/':  out[i++] = '/';  break;
            case 'n':  out[i++] = '\n'; break;
            case 'r':  out[i++] = '\r'; break;
            case 't':  out[i++] = '\t'; break;
            default:   out[i++] = *p;   break;
            }
        } else {
            out[i++] = *p;
        }
        p++;
    }
    out[i] = '\0';
    return 1;
}

/*
 * Find the raw value for `key` inside a JSON object starting at `json`.
 * `json_len` = -1 means NUL-terminated.
 * Returns pointer to value start, sets *out_len to value byte length.
 */
static const char *find_key(const char *json, int json_len,
                             const char *key, int *out_len)
{
    if (!json) return NULL;
    const char *end = (json_len >= 0) ? json + json_len : NULL;

    const char *p = skip_ws(json);
    if (*p != '{') return NULL;
    p++;

    int klen = (int)strlen(key);

    while (*p && (!end || p < end)) {
        p = skip_ws(p);
        if (!*p || *p == '}') break;

        /* Expect a string key */
        if (*p != '"') break;
        /* Check if this key matches */
        const char *key_start = p + 1;
        /* Find end of key */
        const char *ke = key_start;
        while (*ke && *ke != '"') {
            if (*ke == '\\') ke++;
            if (*ke) ke++;
        }
        int this_klen = (int)(ke - key_start);

        p = ke + 1; /* skip closing quote */
        p = skip_ws(p);
        if (*p != ':') break;
        p++;
        p = skip_ws(p);

        /* p now points at the value */
        const char *val_start = p;
        const char *val_end   = skip_value(p);
        if (!val_end) break;

        if (this_klen == klen && memcmp(key_start, key, klen) == 0) {
            if (out_len) *out_len = (int)(val_end - val_start);
            return val_start;
        }

        p = val_end;
        p = skip_ws(p);
        if (*p == ',') p++;
    }
    return NULL;
}

/* ─── Public json_get_* implementations ─────────────────────────────────────── */

int json_get_str(const char *json, const char *key, char *out, int outsz) {
    int vlen;
    const char *v = find_key(json, -1, key, &vlen);
    if (!v) return 0;
    return unquote_str(v, out, outsz);
}

int json_get_int(const char *json, const char *key, long *out) {
    int vlen;
    const char *v = find_key(json, -1, key, &vlen);
    if (!v) return 0;
    v = skip_ws(v);
    if (!isdigit((unsigned char)*v) && *v != '-') return 0;
    *out = atol(v);
    return 1;
}

const char *json_get_raw(const char *json, const char *key, int *len) {
    return find_key(json, -1, key, len);
}

int json_get_str_in(const char *json, int json_len,
                    const char *key, char *out, int outsz)
{
    int vlen;
    const char *v = find_key(json, json_len, key, &vlen);
    if (!v) return 0;
    return unquote_str(v, out, outsz);
}

int json_get_int_in(const char *json, int json_len,
                    const char *key, long *out)
{
    int vlen;
    const char *v = find_key(json, json_len, key, &vlen);
    if (!v) return 0;
    v = skip_ws(v);
    if (!isdigit((unsigned char)*v) && *v != '-') return 0;
    *out = atol(v);
    return 1;
}

const char *json_get_raw_in(const char *json, int json_len,
                             const char *key, int *out_len)
{
    return find_key(json, json_len, key, out_len);
}