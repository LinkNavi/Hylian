# `tar`

```hylian
include { tar, }
```

Source: `stdlib/tar.hy`. Read and extract tar archives (ustar, GNU long names, PAX paths).
Combine with [`inflate`](inflate.md) for `.tar.gz`.

| Function | Signature | Description |
|---|---|---|
| `tar_extract` | `int tar_extract(usize data, int len, str dest, int strip)` | Extract into `dest` (created if needed), dropping `strip` leading path components (like `--strip-components`). Returns the number of regular files written, or `-1` (see `tar_error()`). |
| `tar_list` | `array<str> tar_list(usize data, int len)` | Archived paths of the regular files; `nil` if malformed. |
| `tar_error` | `str tar_error()` | Why the last call failed. |

**Archives are treated as untrusted.** An entry with an absolute path or a `..`
component makes `tar_extract` fail before writing it; symlinks, hard links and special
files are skipped rather than created; permission bits are masked to `0755` (no
setuid/setgid/sticky). Header checksums are verified.

```hylian
include { io, http, inflate, tar, bytes, }

int main() {
    usize r = http_get("http://registry.local/mylib-1.0.tar.gz");
    if (r == 0) { return 1; }
    usize raw = gunzip(http_body_addr(r), http_body_len(r));
    if (raw == 0) { return 1; }
    tar_extract(buf_addr(raw), buf_len(raw), "deps/mylib", 1);
    return 0;
}
```
