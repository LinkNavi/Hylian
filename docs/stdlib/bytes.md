# `bytes`

```hylian
include { bytes, }
```

Source: `stdlib/bytes.hy`. `str` is NUL-terminated, so it can't hold arbitrary binary data
(DNS packets, gzip streams and tar headers all contain `0` bytes). This module works on
a `usize` base address plus an explicit length instead. Nothing is bounds-checked.

| Function | Signature | Description |
|---|---|---|
| `bytes_alloc` / `bytes_free` | `usize bytes_alloc(int n)` / `void bytes_free(usize base, int n)` | Zero-filled buffer of `n` bytes; release it with the same `n`. |
| `get8` / `set8` | `int get8(usize base, int off)` / `void set8(usize base, int off, int v)` | One byte (`0`–`255`). |
| `get16be` / `set16be` | | Big-endian (network order) 16-bit value. |
| `get32be` / `set32be` | | Big-endian 32-bit value. |
| `get64` / `set64` | `int get64(usize base, int off)` | Native-endian 64-bit word. |
| `bytes_copy` | `void bytes_copy(usize dst, int doff, usize src, int soff, int n)` | Copy `n` bytes (regions must not overlap). |
| `bytes_of` | `usize bytes_of(str s)` | Address of a string's bytes. |
| `bytes_put_str` | `int bytes_put_str(usize dst, int doff, str s)` | Copy `s` (no NUL) into a buffer; returns the offset just past it. |
| `bytes_to_str` | `str bytes_to_str(usize base, int off, int n)` | New NUL-terminated `str` from `n` bytes. |
| `bytes_grow` | `usize bytes_grow(usize base, int used, int old_cap, int new_cap)` | Bigger buffer holding the first `used` bytes; frees the old one. |

## Growable buffers

A *Buffer* is an opaque `usize` handle that grows as you append — the natural thing to
feed from `recv`.

| Function | Description |
|---|---|
| `buf_new()` | Empty buffer. |
| `buf_addr(b)` / `buf_len(b)` | Current contents (address may change after appends). |
| `buf_append(b, src, soff, n)` / `buf_append_str(b, s)` | Append bytes / a string's characters. |
| `buf_reserve(b, n)` + `buf_commit(b, n)` | Reserve room for `n` more bytes and get the write address; commit how many were actually written (e.g. what `recv_bytes` returned). |
| `buf_free(b)` | Release it. |
