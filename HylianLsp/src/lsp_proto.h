#ifndef LSP_PROTO_H
#define LSP_PROTO_H

#include <stdio.h>
#include <stddef.h>

/* ─── JSON-RPC / LSP framing ─────────────────────────────────────────────────
   LSP uses HTTP-style headers over stdin/stdout:

     Content-Length: 123\r\n
     \r\n
     { "jsonrpc": "2.0", ... }

   We provide:
     lsp_read_message()  – block until a full message arrives, return heap buf
     lsp_write_message() – frame and write a JSON string to stdout
   ─────────────────────────────────────────────────────────────────────────── */

/*
 * Read one complete LSP message from `in`.
 * Blocks until Content-Length bytes have been received.
 * Returns a NUL-terminated heap buffer the caller must free(), or NULL on EOF
 * or unrecoverable error.
 */
char *lsp_read_message(FILE *in);

/*
 * Write one complete LSP message to `out`.
 * Adds the Content-Length header, \r\n separator, then the body.
 * `json` must be a NUL-terminated JSON string.
 */
void lsp_write_message(FILE *out, const char *json);

/* ─── Minimal JSON builder ────────────────────────────────────────────────────
   We avoid pulling in a heavy JSON library.  Instead we build responses with a
   simple append-buffer helper.

   Usage:
       JsonBuf b;
       jb_init(&b);
       jb_obj_begin(&b);
         jb_key_str(&b, "jsonrpc", "2.0");
         jb_key_str(&b, "id",      "1");
         jb_key_begin_obj(&b, "result");
           jb_key_str(&b, "foo", "bar");
         jb_obj_end(&b);
       jb_obj_end(&b);
       lsp_write_message(stdout, b.buf);
       jb_free(&b);
   ─────────────────────────────────────────────────────────────────────────── */

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
    /* comma-tracking stack: whether the current object/array needs a comma
       before the next item */
    int need_comma[64];
    int depth;
} JsonBuf;

void jb_init(JsonBuf *b);
void jb_free(JsonBuf *b);

/* Raw append — you rarely need to call this directly */
void jb_raw(JsonBuf *b, const char *s);

/* Begin / end an object  { } */
void jb_obj_begin(JsonBuf *b);
void jb_obj_end(JsonBuf *b);

/* Begin / end an array  [ ] */
void jb_arr_begin(JsonBuf *b);
void jb_arr_end(JsonBuf *b);

/* Emit a key (string) inside an object */
void jb_key(JsonBuf *b, const char *key);

/* Composite helpers: "key": value */
void jb_key_str(JsonBuf *b, const char *key, const char *val);
void jb_key_int(JsonBuf *b, const char *key, long val);
void jb_key_bool(JsonBuf *b, const char *key, int val);  /* 0 = false */
void jb_key_null(JsonBuf *b, const char *key);

/* Start a nested object/array under a key */
void jb_key_begin_obj(JsonBuf *b, const char *key);
void jb_key_begin_arr(JsonBuf *b, const char *key);

/* Array element helpers (no key — use inside jb_arr_begin / jb_arr_end) */
void jb_arr_str(JsonBuf *b, const char *val);
void jb_arr_int(JsonBuf *b, long val);
void jb_arr_begin_obj(JsonBuf *b);   /* push a { inside an array */

/* ─── Minimal JSON reader ────────────────────────────────────────────────────
   We only need to extract a small set of fields from incoming LSP messages.
   Rather than a full parser we provide targeted extraction helpers that work
   on raw JSON text via string scanning.
   ─────────────────────────────────────────────────────────────────────────── */

/*
 * Extract the string value of a top-level key from a flat JSON object.
 * e.g. json_get_str(msg, "method", buf, sizeof(buf))
 * Returns 1 on success, 0 if the key was not found or value is not a string.
 * The extracted value is written (unescaped) into `out`.
 */
int json_get_str(const char *json, const char *key, char *out, int outsz);

/*
 * Extract an integer value of a top-level key.
 * Returns 1 on success, 0 if not found or not an integer.
 */
int json_get_int(const char *json, const char *key, long *out);

/*
 * Locate the raw JSON value (object, array, string, number, …) for a key.
 * Returns a pointer into `json` at the start of the value, or NULL.
 * `len` is set to the byte length of the value token.
 */
const char *json_get_raw(const char *json, const char *key, int *len);

/*
 * Given a raw JSON value that is an object, extract one of its keys.
 * e.g.:
 *   const char *params = json_get_raw(msg, "params", &len);
 *   json_get_str_in(params, len, "uri", buf, sizeof(buf));
 */
int json_get_str_in(const char *json, int json_len,
                    const char *key, char *out, int outsz);
int json_get_int_in(const char *json, int json_len,
                    const char *key, long *out);

/*
 * Locate a nested key inside a sub-object returned by json_get_raw().
 * Returns a pointer to the value's raw JSON text, sets *len.
 */
const char *json_get_raw_in(const char *json, int json_len,
                             const char *key, int *out_len);

#endif /* LSP_PROTO_H */