# `http`

```hylian
include { http, }
```

Source: `stdlib/http.hy`. An HTTP/1.1 client over plain TCP. **`http://` only** — for
`https://` use [`https`](https.md).

Handles hostname lookup ([`dns`](dns.md)), `Content-Length`, chunked
transfer-encoding, read-until-close bodies, and redirects (301/302/303/307/308, up to
5 hops; absolute URLs or same-host `/path`). Request timeout is 20 s per read.

A `Response` is a struct (`status`, `body`, `body_len`, plus header arrays read through
`http_header`); a failed request is the `Error?` half of the `(Response, Error?)` result.
Bodies are length-carrying, so binary payloads are safe (use `r.body` / `r.body_len`).

| Function | Signature | Description |
|---|---|---|
| `http_get` | `(Response, Error?) http_get(str url)` | GET. |
| `http_post` | `(Response, Error?) http_post(str url, str content_type, str body)` | POST. |
| `http_request` | `(Response, Error?) http_request(str method, str url, array<str> headers, str body)` | Any method; `headers` are `"Name: value"` lines; `body` may be `nil`. |
| `http_header` | `str http_header(Response r, str name)` | Header value (case-insensitive name), `""` if absent. |
| `http_body` | `str http_body(Response r)` | Body as a `str` copy (text). |
| `http_save_body` | `bool http_save_body(Response r, str path)` | Write the body to a file. |
| `http_download` | `Error? http_download(str url, str path)` | GET and save a `200` response. |
| `http_free` | `void http_free(Response r)` | Release a response. |
| `url_parse` | `(Url, Error?) url_parse(str url)` | `Url { scheme, host, port, path }`. |

Lower-level pieces (`http_build_request`, `http_parse_response`, `http_exchange`,
`http_redirect_url`) are public too; `https` reuses them for its TLS transport.

```hylian
include { io, http, }

int main() {
    r, err := http_get("http://example.com/");
    if (err != nil) { eprintln(err.message()); return 1; }
    println(http_body(r));
    http_free(r);
    return 0;
}
```
