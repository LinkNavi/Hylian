## std.crypto

Cryptographic primitives backed by OpenSSL: hashing, HMAC, authenticated
encryption (AES-256-GCM), a CSPRNG, and constant-time comparison.

```hylian
include {
    std.crypto,
}
```

Linking requires the OpenSSL libraries:

```sh
-lssl -lcrypto
```

> **Prefix note:** Functions in `std.crypto` are written with the `crypto_`
> prefix in Hylian source. The compiler automatically rewrites them to the
> `hylian_crypto_` ABI symbol at link time. You never write `hylian_crypto_`
> yourself.

---

## Hashing

Compute a one-way digest of an input using a named algorithm.

**Supported algorithms:**

| Algorithm | Raw bytes | Hex characters |
|---|---|---|
| `"sha256"` | 32 | 64 |
| `"sha512"` | 64 | 128 |
| `"sha1"` | 20 | 40 |
| `"md5"` | 16 | 32 |

> **Note:** SHA-1 and MD5 are provided for interoperability with legacy
> systems. Prefer SHA-256 or SHA-512 for any new security-sensitive work.

---

### `crypto_hash(algo: str, input: str, len: int, out_buf: str, out_buflen: int) -> int`

Hash `len` bytes of `input` using the named algorithm and write the raw digest
bytes into `out_buf`.

| Parameter | Type | Description |
|---|---|---|
| `algo` | `str` | Algorithm name: `"sha256"`, `"sha512"`, `"sha1"`, or `"md5"` |
| `input` | `str` | Data to hash |
| `len` | `int` | Byte length of `input` |
| `out_buf` | `str` | Caller-supplied output buffer |
| `out_buflen` | `int` | Size of `out_buf` in bytes — must be at least the digest size for the chosen algorithm (see table above) |

**Return value:** Number of raw bytes written into `out_buf`. Returns `-1` if
the algorithm name is unrecognised, if `out_buf` is too small, or on any
internal OpenSSL error.

```hylian
include {
    std.io,
    std.crypto,
}

Error? main() {
    str input = "Hello, Hylian!";
    int  len   = 14;

    str digest;
    int written = crypto_hash("sha256", input, len, digest, 32);

    if (written == -1) {
        panic("hashing failed");
    }

    println("SHA-256 digest written ({{written}} bytes).");
    return nil;
}
```

---

### `crypto_hash_hex(algo: str, input: str, len: int, out_buf: str, out_buflen: int) -> int`

Like `crypto_hash` but writes a lowercase hex-encoded string (null-terminated)
into `out_buf` instead of raw bytes.

| Parameter | Type | Description |
|---|---|---|
| `algo` | `str` | Algorithm name: `"sha256"`, `"sha512"`, `"sha1"`, or `"md5"` |
| `input` | `str` | Data to hash |
| `len` | `int` | Byte length of `input` |
| `out_buf` | `str` | Caller-supplied output buffer |
| `out_buflen` | `int` | Size of `out_buf` — must be at least `digest_bytes * 2 + 1` (see table above) |

**Return value:** Number of hex characters written (not counting the null
terminator). Returns `-1` on any error.

```hylian
include {
    std.io,
    std.crypto,
}

Error? main() {
    str input = "Hello, Hylian!";
    int  len   = 14;

    str hex_out;
    int chars = crypto_hash_hex("sha256", input, len, hex_out, 65);

    if (chars == -1) {
        panic("hashing failed");
    }

    println("SHA-256: {{hex_out}}");
    return nil;
}
```

Output:

```sh
SHA-256: <64-character lowercase hex string>
```

---

## HMAC

Compute a Hash-based Message Authentication Code to verify both the integrity
and the authenticity of a message. The same algorithm names accepted by
`crypto_hash` are supported.

---

### `crypto_hmac(algo: str, key: str, key_len: int, input: str, input_len: int, out_buf: str, out_buflen: int) -> int`

Compute the raw HMAC bytes of `input` under `key` using the named digest
algorithm.

| Parameter | Type | Description |
|---|---|---|
| `algo` | `str` | Digest algorithm: `"sha256"`, `"sha512"`, `"sha1"`, or `"md5"` |
| `key` | `str` | The secret key |
| `key_len` | `int` | Byte length of `key` |
| `input` | `str` | Message to authenticate |
| `input_len` | `int` | Byte length of `input` |
| `out_buf` | `str` | Caller-supplied output buffer |
| `out_buflen` | `int` | Size of `out_buf` — must be at least the digest size for the chosen algorithm |

**Return value:** Number of raw bytes written into `out_buf`. Returns `-1` if
the algorithm name is unrecognised, if `out_buf` is too small, or on any
internal OpenSSL error.

```hylian
include {
    std.io,
    std.crypto,
}

Error? main() {
    str key     = "supersecretkey";
    int key_len = 14;
    str msg     = "authenticate me";
    int msg_len = 15;

    str mac;
    int written = crypto_hmac("sha256", key, key_len, msg, msg_len, mac, 32);

    if (written == -1) {
        panic("HMAC failed");
    }

    println("HMAC-SHA256 computed ({{written}} bytes).");
    return nil;
}
```

---

### `crypto_hmac_hex(algo: str, key: str, key_len: int, input: str, input_len: int, out_buf: str, out_buflen: int) -> int`

Like `crypto_hmac` but writes a lowercase hex string (null-terminated) into
`out_buf`.

| Parameter | Type | Description |
|---|---|---|
| `algo` | `str` | Digest algorithm: `"sha256"`, `"sha512"`, `"sha1"`, or `"md5"` |
| `key` | `str` | The secret key |
| `key_len` | `int` | Byte length of `key` |
| `input` | `str` | Message to authenticate |
| `input_len` | `int` | Byte length of `input` |
| `out_buf` | `str` | Caller-supplied output buffer |
| `out_buflen` | `int` | Size of `out_buf` — must be at least `digest_bytes * 2 + 1` |

**Return value:** Number of hex characters written (not counting the null
terminator). Returns `-1` on any error.

```hylian
include {
    std.io,
    std.crypto,
}

Error? main() {
    str key     = "supersecretkey";
    int key_len = 14;
    str msg     = "authenticate me";
    int msg_len = 15;

    str hex_out;
    int chars = crypto_hmac_hex("sha256", key, key_len, msg, msg_len, hex_out, 65);

    if (chars == -1) {
        panic("HMAC failed");
    }

    println("HMAC-SHA256: {{hex_out}}");
    return nil;
}
```

> **Comparing HMACs:** Never compare HMAC values with `hylian_equals` or `==`.
> Use `crypto_constant_time_eq` to prevent timing attacks. See the
> [Utilities](#utilities) section below.

---

## Authenticated Encryption (AES-256-GCM)

AES-256-GCM provides both confidentiality and authenticity in a single
operation. The 16-byte GCM authentication tag is appended to the ciphertext
by `crypto_encrypt` and verified automatically by `crypto_decrypt`.

**Key and IV requirements:**

| Field | Required size | How to generate |
|---|---|---|
| Key | Exactly **32 bytes** | `crypto_random_bytes` |
| IV (nonce) | Exactly **12 bytes** | `crypto_random_bytes` |

> **Critical:** Never reuse an IV with the same key. Each encryption call must
> use a freshly generated IV. Reusing an IV completely breaks GCM's
> confidentiality and authentication guarantees.

---

### `crypto_encrypt(key: str, key_len: int, iv: str, iv_len: int, plaintext: str, pt_len: int, out_buf: str, out_buflen: int) -> int`

Encrypt `plaintext` using AES-256-GCM. The output written to `out_buf` is the
ciphertext followed immediately by the 16-byte authentication tag.

| Parameter | Type | Description |
|---|---|---|
| `key` | `str` | Encryption key — must be exactly 32 bytes |
| `key_len` | `int` | Must be `32` |
| `iv` | `str` | Initialisation vector — must be exactly 12 bytes |
| `iv_len` | `int` | Must be `12` |
| `plaintext` | `str` | Data to encrypt |
| `pt_len` | `int` | Byte length of `plaintext` |
| `out_buf` | `str` | Caller-supplied output buffer |
| `out_buflen` | `int` | Must be at least `pt_len + 16` bytes |

**Output buffer layout:**

```
out_buf[0 .. pt_len - 1]        — ciphertext  (same length as plaintext)
out_buf[pt_len .. pt_len + 15]  — 16-byte GCM authentication tag
```

**Return value:** Total bytes written (`pt_len + 16`). Returns `-1` if
`key_len != 32`, `iv_len != 12`, `out_buflen < pt_len + 16`, or on any
internal OpenSSL error.

---

### `crypto_decrypt(key: str, key_len: int, iv: str, iv_len: int, ciphertext: str, ct_len: int, out_buf: str, out_buflen: int) -> int`

Decrypt AES-256-GCM ciphertext and verify the authentication tag. The input
`ciphertext` must be the exact byte sequence produced by `crypto_encrypt` —
that is, the ciphertext bytes followed by the 16-byte tag.

| Parameter | Type | Description |
|---|---|---|
| `key` | `str` | Decryption key — must be exactly 32 bytes and match the encryption key |
| `key_len` | `int` | Must be `32` |
| `iv` | `str` | Initialisation vector — must be exactly 12 bytes and match the encryption IV |
| `iv_len` | `int` | Must be `12` |
| `ciphertext` | `str` | Ciphertext bytes followed by the 16-byte auth tag |
| `ct_len` | `int` | Byte length of the ciphertext portion only (not including the tag) |
| `out_buf` | `str` | Caller-supplied output buffer |
| `out_buflen` | `int` | Must be at least `ct_len` bytes |

**Return value:** Plaintext length (`ct_len`) on success. Returns `-1` if the
authentication tag does not match (indicating tampering or a wrong key/IV),
if `key_len != 32`, `iv_len != 12`, or on any internal OpenSSL error.

> **Always check the return value.** A return of `-1` means the data must not
> be trusted or used.

---

## Random (CSPRNG)

All random functions draw entropy from OpenSSL's `RAND_bytes`, which is
seeded from the operating system's cryptographically secure entropy source.
These are suitable for key generation, nonce generation, and any other
security-sensitive purpose.

---

### `crypto_random_bytes(buf: str, len: int) -> int`

Fill `buf` with `len` cryptographically secure random bytes.

| Parameter | Type | Description |
|---|---|---|
| `buf` | `str` | Caller-supplied buffer to fill |
| `len` | `int` | Number of random bytes to write |

**Return value:** `len` on success. Returns `-1` if `buf` is null, `len` is
less than or equal to `0`, or the RNG fails.

```hylian
include {
    std.io,
    std.crypto,
}

Error? main() {
    str key;
    int ok = crypto_random_bytes(key, 32);

    if (ok == -1) {
        panic("RNG failure");
    }

    println("Generated 32-byte random key.");
    return nil;
}
```

---

### `crypto_random_int(max: int) -> int`

Return a cryptographically secure random integer in the range `[0, max)`.
Uses rejection sampling to eliminate modulo bias — every value in the range
is equally likely.

| Parameter | Type | Description |
|---|---|---|
| `max` | `int` | Upper bound (exclusive). Must be greater than `0`. |

**Return value:** A random integer in `[0, max)`. Returns `-1` if `max <= 0`
or if the RNG fails.

```hylian
include {
    std.io,
    std.crypto,
}

Error? main() {
    int roll = crypto_random_int(6);

    if (roll == -1) {
        panic("RNG failure");
    }

    int face = roll + 1;
    println("You rolled: {{face}}");
    return nil;
}
```

---

### `crypto_random_float() -> int`

Return a cryptographically secure random floating-point value in `[0.0, 1.0)`.

Because Hylian's ABI passes all return values as `int`, this function returns
the **bit-pattern** of the `double` result reinterpreted as an `int64`. Cast
it back to a `float` in Hylian using `bits_to_float`.

| Parameter | Type | Description |
|---|---|---|
| *(none)* | — | — |

**Return value:** Bit-pattern of a `double` in `[0.0, 1.0)` stored as `int`.
Returns `-1` (bit-pattern) on RNG failure — check for this before casting.

```hylian
include {
    std.io,
    std.crypto,
}

Error? main() {
    int bits = crypto_random_float();
    float f  = bits_to_float(bits);

    println("Random float in [0, 1): generated successfully.");
    return nil;
}
```

---

## Utilities

### `crypto_constant_time_eq(a: str, b: str, len: int) -> int`

Compare the first `len` bytes of `a` and `b` in constant time. The comparison
takes the same amount of time regardless of where (or whether) the buffers
differ, preventing an attacker from learning information through timing
measurements.

| Parameter | Type | Description |
|---|---|---|
| `a` | `str` | First buffer |
| `b` | `str` | Second buffer |
| `len` | `int` | Number of bytes to compare |

**Return value:** `1` if the first `len` bytes of both buffers are identical,
`0` if they differ. Returns `0` if either pointer is null or `len <= 0`.

> **Always use this function — not `hylian_equals` or `==` — when comparing
> MACs, digests, tokens, or any other secret value.** Ordinary string
> comparison short-circuits on the first differing byte, which leaks timing
> information that an attacker can exploit.

```hylian
include {
    std.io,
    std.crypto,
}

Error? main() {
    str key     = "supersecretkey";
    int key_len = 14;
    str msg     = "important message";
    int msg_len = 17;

    str mac_a;
    str mac_b;

    crypto_hmac("sha256", key, key_len, msg, msg_len, mac_a, 32);
    crypto_hmac("sha256", key, key_len, msg, msg_len, mac_b, 32);

    int eq = crypto_constant_time_eq(mac_a, mac_b, 32);

    if (eq) {
        println("MACs match — message is authentic.");
    } else {
        panic("MAC mismatch — message has been tampered with.");
    }

    return nil;
}
```

---

## Complete example — generate key, encrypt, decrypt, verify round-trip

This example shows the full AES-256-GCM workflow:

1. Generate a random 32-byte key and a random 12-byte IV using `crypto_random_bytes`.
2. Encrypt a plaintext string with `crypto_encrypt`.
3. Decrypt the result with `crypto_decrypt`.
4. Verify that the recovered plaintext matches the original using `hylian_equals`.

```hylian
include {
    std.io,
    std.strings,
    std.crypto,
}

Error? main() {
    // ── 1. Generate a random key and IV ─────────────────────────────────────

    str key;
    str iv;

    if (crypto_random_bytes(key, 32) == -1) {
        panic("failed to generate key");
    }
    if (crypto_random_bytes(iv, 12) == -1) {
        panic("failed to generate IV");
    }

    println("Key and IV generated.");

    // ── 2. Encrypt ──────────────────────────────────────────────────────────

    str plaintext    = "Hello from Hylian!";
    int pt_len       = 18;

    // out_buf must hold pt_len ciphertext bytes + 16 tag bytes
    str cipher_buf;
    int enc_len = crypto_encrypt(key, 32, iv, 12, plaintext, pt_len, cipher_buf, 34);

    if (enc_len == -1) {
        panic("encryption failed");
    }

    println("Encrypted {{pt_len}} bytes -> {{enc_len}} bytes (ciphertext + tag).");

    // ── 3. Decrypt ──────────────────────────────────────────────────────────

    // ct_len is the ciphertext length only — NOT including the 16-byte tag.
    // crypto_encrypt returns pt_len + 16, so ciphertext length = enc_len - 16.
    int ct_len = enc_len - 16;

    str plain_buf;
    int dec_len = crypto_decrypt(key, 32, iv, 12, cipher_buf, ct_len, plain_buf, ct_len);

    if (dec_len == -1) {
        panic("decryption failed — bad key, IV, or tampered ciphertext");
    }

    println("Decrypted {{dec_len}} bytes.");

    // ── 4. Verify round-trip ─────────────────────────────────────────────

    int match = hylian_equals(plaintext, plain_buf);

    if (match) {
        println("Round-trip verified: plaintext matches recovered plaintext.");
    } else {
        panic("round-trip failed: recovered plaintext does not match");
    }

    return nil;
}
```

Expected output:

```sh
Key and IV generated.
Encrypted 18 bytes -> 34 bytes (ciphertext + tag).
Decrypted 18 bytes.
Round-trip verified: plaintext matches recovered plaintext.
```
```

Now let me verify the file was created and check all four files exist: