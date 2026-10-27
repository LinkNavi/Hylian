# `https`

```hylian
include { https, }
```

Source: `stdlib/https.hy`. [`http`](http.md) over TLS, plus a scheme-agnostic `fetch`. It
returns the same `Response` and uses the same accessors (`r.status`,
`http_body`, `r.body`, `http_free`, …), and follows redirects across schemes.

| Function | Signature | Description |
|---|---|---|
| `https_get` / `fetch` | `(Response, Error?) https_get(str url)` | GET an `http://` **or** `https://` URL. |
| `https_request` | `(Response, Error?) https_request(str method, str url, array<str> headers, str body)` | Any method. |
| `https_download` | `Error? https_download(str url, str path)` | GET and save a `200` response. |

Certificates are verified — see [`tls`](tls.md). Linking this module needs OpenSSL:

```toml
# Zora.toml
[[target.myapp.deps]]
system = "openssl"
```

```hylian
include { io, https, }

int main() {
    r, err := fetch("https://example.com/");
    if (err != nil) { eprintln(err.message()); return 1; }
    println(http_body(r));
    return 0;
}
```
