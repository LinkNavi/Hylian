#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/err.h>

// ── hylian_crypto_hash ────────────────────────────────────────────────────────
//
// Hash `input` (len bytes) using the named algorithm (e.g. "sha256", "sha512",
// "md5", "sha1"). Writes the raw digest bytes into `out_buf` (caller-supplied,
// must be at least 64 bytes). Returns the number of bytes written, or -1 on
// error.

int64_t hylian_crypto_hash(char *algorithm, char *input, int64_t len,
                            char *out_buf, int64_t out_buflen) {
    if (!algorithm || !input || !out_buf || len < 0 || out_buflen < 1)
        return -1;

    const EVP_MD *md = EVP_get_digestbyname(algorithm);
    if (!md) return -1;

    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx) return -1;

    unsigned int digest_len = 0;
    unsigned char digest[EVP_MAX_MD_SIZE];

    int ok = EVP_DigestInit_ex(ctx, md, NULL) &&
             EVP_DigestUpdate(ctx, input, (size_t)len) &&
             EVP_DigestFinal_ex(ctx, digest, &digest_len);
    EVP_MD_CTX_free(ctx);

    if (!ok) return -1;
    if ((int64_t)digest_len > out_buflen) return -1;

    memcpy(out_buf, digest, digest_len);
    return (int64_t)digest_len;
}

// ── hylian_crypto_hash_hex ────────────────────────────────────────────────────
//
// Like hylian_crypto_hash but writes a lowercase hex string (null-terminated)
// into out_buf. out_buflen must be at least digest_bytes*2 + 1.
// Returns the number of hex characters written (not counting the null), or -1.

int64_t hylian_crypto_hash_hex(char *algorithm, char *input, int64_t len,
                                char *out_buf, int64_t out_buflen) {
    if (!out_buf || out_buflen < 3) return -1;

    unsigned char raw[EVP_MAX_MD_SIZE];
    int64_t raw_len = hylian_crypto_hash(algorithm, input, len,
                                          (char *)raw, sizeof(raw));
    if (raw_len < 0) return -1;

    if (out_buflen < raw_len * 2 + 1) return -1;

    static const char hex[] = "0123456789abcdef";
    for (int64_t i = 0; i < raw_len; i++) {
        out_buf[i * 2]     = hex[(raw[i] >> 4) & 0xf];
        out_buf[i * 2 + 1] = hex[raw[i] & 0xf];
    }
    out_buf[raw_len * 2] = '\0';
    return raw_len * 2;
}

// ── hylian_crypto_hmac ────────────────────────────────────────────────────────
//
// Compute HMAC of `input` using `key` and the named digest algorithm.
// Writes raw bytes into out_buf. Returns bytes written, or -1 on error.

int64_t hylian_crypto_hmac(char *algorithm,
                            char *key,   int64_t key_len,
                            char *input, int64_t input_len,
                            char *out_buf, int64_t out_buflen) {
    if (!algorithm || !key || !input || !out_buf) return -1;
    if (key_len < 0 || input_len < 0 || out_buflen < 1) return -1;

    const EVP_MD *md = EVP_get_digestbyname(algorithm);
    if (!md) return -1;

    unsigned int out_len = 0;
    unsigned char digest[EVP_MAX_MD_SIZE];

    unsigned char *result = HMAC(md,
                                  key,   (int)key_len,
                                  (unsigned char *)input, (int)input_len,
                                  digest, &out_len);
    if (!result) return -1;
    if ((int64_t)out_len > out_buflen) return -1;

    memcpy(out_buf, digest, out_len);
    return (int64_t)out_len;
}

// ── hylian_crypto_hmac_hex ────────────────────────────────────────────────────
//
// Like hylian_crypto_hmac but returns a lowercase hex string.
// Returns hex char count, or -1 on error.

int64_t hylian_crypto_hmac_hex(char *algorithm,
                                char *key,   int64_t key_len,
                                char *input, int64_t input_len,
                                char *out_buf, int64_t out_buflen) {
    if (!out_buf || out_buflen < 3) return -1;

    unsigned char raw[EVP_MAX_MD_SIZE];
    int64_t raw_len = hylian_crypto_hmac(algorithm, key, key_len,
                                          input, input_len,
                                          (char *)raw, sizeof(raw));
    if (raw_len < 0) return -1;
    if (out_buflen < raw_len * 2 + 1) return -1;

    static const char hex[] = "0123456789abcdef";
    for (int64_t i = 0; i < raw_len; i++) {
        out_buf[i * 2]     = hex[(raw[i] >> 4) & 0xf];
        out_buf[i * 2 + 1] = hex[raw[i] & 0xf];
    }
    out_buf[raw_len * 2] = '\0';
    return raw_len * 2;
}

// ── hylian_crypto_encrypt ─────────────────────────────────────────────────────
//
// Encrypt `plaintext` (pt_len bytes) using AES-256-GCM.
//   key      — must be exactly 32 bytes
//   iv       — must be exactly 12 bytes (96-bit GCM nonce)
//   out_buf  — caller-supplied; must be at least pt_len + 16 bytes
//              (ciphertext is same length as plaintext; 16-byte auth tag appended)
//
// Layout of out_buf on success:
//   [0 .. pt_len-1]          ciphertext
//   [pt_len .. pt_len+15]    16-byte GCM auth tag
//
// Returns total bytes written (pt_len + 16), or -1 on error.

int64_t hylian_crypto_encrypt(char *key,  int64_t key_len,
                               char *iv,   int64_t iv_len,
                               char *plaintext, int64_t pt_len,
                               char *out_buf,   int64_t out_buflen) {
    if (!key || !iv || !plaintext || !out_buf) return -1;
    if (key_len != 32 || iv_len != 12) return -1;
    if (pt_len < 0) return -1;
    if (out_buflen < pt_len + 16) return -1;

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return -1;

    int ok = 1;
    int out_len = 0;
    int final_len = 0;

    ok = ok && EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL);
    ok = ok && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, NULL);
    ok = ok && EVP_EncryptInit_ex(ctx, NULL, NULL,
                                   (unsigned char *)key, (unsigned char *)iv);
    ok = ok && EVP_EncryptUpdate(ctx, (unsigned char *)out_buf, &out_len,
                                  (unsigned char *)plaintext, (int)pt_len);
    ok = ok && EVP_EncryptFinal_ex(ctx, (unsigned char *)out_buf + out_len, &final_len);

    if (ok) {
        // Append the 16-byte auth tag after the ciphertext
        ok = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16,
                                  (unsigned char *)out_buf + out_len + final_len);
    }

    EVP_CIPHER_CTX_free(ctx);
    if (!ok) return -1;
    return (int64_t)(out_len + final_len + 16);
}

// ── hylian_crypto_decrypt ─────────────────────────────────────────────────────
//
// Decrypt AES-256-GCM ciphertext.
//   key       — 32 bytes
//   iv        — 12 bytes
//   ciphertext — ct_len bytes of ciphertext followed by 16 bytes of auth tag
//                (i.e. total input is ct_len + 16 bytes from hylian_crypto_encrypt)
//   out_buf   — caller-supplied; at least ct_len bytes
//
// Returns plaintext length (ct_len) on success, or -1 if decryption or tag
// verification fails.

int64_t hylian_crypto_decrypt(char *key,  int64_t key_len,
                               char *iv,   int64_t iv_len,
                               char *ciphertext, int64_t ct_len,
                               char *out_buf,    int64_t out_buflen) {
    if (!key || !iv || !ciphertext || !out_buf) return -1;
    if (key_len != 32 || iv_len != 12) return -1;
    if (ct_len < 0) return -1;
    if (out_buflen < ct_len) return -1;

    // The auth tag is the last 16 bytes of the input buffer
    unsigned char *tag = (unsigned char *)ciphertext + ct_len;

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return -1;

    int ok = 1;
    int out_len = 0;
    int final_len = 0;

    ok = ok && EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL);
    ok = ok && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, NULL);
    ok = ok && EVP_DecryptInit_ex(ctx, NULL, NULL,
                                   (unsigned char *)key, (unsigned char *)iv);
    ok = ok && EVP_DecryptUpdate(ctx, (unsigned char *)out_buf, &out_len,
                                  (unsigned char *)ciphertext, (int)ct_len);
    ok = ok && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, tag);
    // EVP_DecryptFinal_ex returns 0 if tag verification fails
    ok = ok && (EVP_DecryptFinal_ex(ctx, (unsigned char *)out_buf + out_len,
                                     &final_len) > 0);

    EVP_CIPHER_CTX_free(ctx);
    if (!ok) return -1;
    return (int64_t)(out_len + final_len);
}

// ── hylian_crypto_random_bytes ────────────────────────────────────────────────
//
// Fill `buf` with `len` cryptographically secure random bytes via OpenSSL's
// RAND_bytes. Returns len on success, -1 on failure.

int64_t hylian_crypto_random_bytes(char *buf, int64_t len) {
    if (!buf || len <= 0) return -1;
    if (RAND_bytes((unsigned char *)buf, (int)len) != 1)
        return -1;
    return len;
}

// ── hylian_crypto_random_int ──────────────────────────────────────────────────
//
// Return a cryptographically secure random int64 in [0, max).
// Returns -1 if max <= 0 or on RNG failure.

int64_t hylian_crypto_random_int(int64_t max) {
    if (max <= 0) return -1;

    uint64_t raw;
    if (RAND_bytes((unsigned char *)&raw, sizeof(raw)) != 1)
        return -1;

    // Rejection sampling to avoid modulo bias.
    // Compute the largest multiple of max that fits in uint64.
    uint64_t umax = (uint64_t)max;
    uint64_t limit = (UINT64_MAX - umax + 1) % umax; // = UINT64_MAX - (UINT64_MAX % umax)

    while (raw < limit) {
        if (RAND_bytes((unsigned char *)&raw, sizeof(raw)) != 1)
            return -1;
    }

    return (int64_t)(raw % umax);
}

// ── hylian_crypto_random_float ────────────────────────────────────────────────
//
// Return a cryptographically secure random double in [0.0, 1.0).
// Returns -1.0 on RNG failure (cast to int64_t for ABI; caller interprets).

int64_t hylian_crypto_random_float(void) {
    uint64_t raw;
    if (RAND_bytes((unsigned char *)&raw, sizeof(raw)) != 1)
        return -1;

    // Use top 53 bits (mantissa width of double) for uniform [0,1)
    double f = (double)(raw >> 11) / (double)(UINT64_C(1) << 53);

    // Return bit-pattern of double so it survives the int64 ABI boundary.
    int64_t result;
    memcpy(&result, &f, sizeof(result));
    return result;
}

// ── hylian_crypto_constant_time_eq ───────────────────────────────────────────
//
// Compare two byte buffers of equal length in constant time.
// Returns 1 if equal, 0 if not. Safe for comparing MACs/digests.

int64_t hylian_crypto_constant_time_eq(char *a, char *b, int64_t len) {
    if (!a || !b || len <= 0) return 0;
    unsigned char diff = 0;
    for (int64_t i = 0; i < len; i++)
        diff |= (unsigned char)a[i] ^ (unsigned char)b[i];
    return diff == 0 ? 1 : 0;
}