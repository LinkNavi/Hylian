# `https`

```hylian
include { https, }
```

Source: `stdlib/https.hy`. [`http`](http.md) over TLS, plus a scheme-agnostic `fetch`. It
returns the same response handle and uses the same accessors (`http_status`,
`http_body`, `http_body_addr`, `http_free`, …), and follows redirects across schemes.

| Function | Signature | Description |
|---|---|---|
| `https_get` / `fetch` | `usize https_get(str url)` | GET an `http://` **or** `https://` URL. |
| `https_request` | `usize https_request(str method, str url, array<str> headers, str body)` | Any method. |
| `https_download` | `bool https_download(str url, str path)` | GET and save a `200` response. |
| `https_error` | `str https_error()` | Description of the last failure. |

Certificates are verified — see [`tls`](tls.md). Linking this module needs OpenSSL:

```toml
# Zora.toml
[[target.myapp.deps]]
system = "openssl"
```

```hylian
include { io, https, }

int main() {
    usize r = fetch("https://example.com/");
    if (r == 0) { eprintln(https_error()); return 1; }
    println(http_body(r));
    return 0;
}
```
