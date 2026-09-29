# `tls`

```hylian
include { tls, }
```

Source: `stdlib/tls.hy`. TLS client connections, implemented by driving the **system
OpenSSL** (`libssl`) through the hand-written binding `stdlib/openssl.hyi`
(`std.openssl`). Hylian has no crypto of its own; this module only orchestrates
OpenSSL, so it needs `libssl`/`libcrypto` at link time (in a Zora project add
`[[target.<name>.deps]]` with `system = "openssl"`).

**Certificates are always verified**: the chain must validate against the system trust
store (or `$SSL_CERT_FILE` / `$SSL_CERT_DIR`, or a CA added with `tls_load_ca_file`),
and must match the host (DNS name or literal IP). The host is also sent as SNI. There
is deliberately no switch to turn verification off.

| Function | Signature | Description |
|---|---|---|
| `tls_connect` | `usize tls_connect(str ip, int port, str host)` | TCP-connect to `ip:port`, then handshake expecting a certificate for `host`. Handle, or `0` (see `tls_error()`). |
| `tls_send` | `bool tls_send(usize c, usize buf, int len)` | Send all `len` bytes. |
| `tls_recv` | `int tls_recv(usize c, usize buf, int cap)` | Bytes read, `0` on peer close, negative on error/timeout. |
| `tls_close` | `void tls_close(usize c)` | Shut down and release. |
| `tls_load_ca_file` | `bool tls_load_ca_file(str path)` | Also trust the CAs in a PEM file. |
| `tls_error` | `str tls_error()` | Why the last `tls_connect` failed. |

A peer that closes without a TLS `close_notify` is treated as a normal end of stream;
truncation is caught by HTTP's `Content-Length` / chunked framing.
