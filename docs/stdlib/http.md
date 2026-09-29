# `http`

```hylian
include { http, }
```

Source: `stdlib/http.hy`. An HTTP/1.1 client over plain TCP. **`http://` only** — for
`https://` use [`https`](https.md).

Handles hostname lookup ([`dns`](dns.md)), `Content-Length`, chunked
transfer-encoding, read-until-close bodies, and redirects (301/302/303/307/308, up to
5 hops; absolute URLs or same-host `/path`). Request timeout is 20 s per read.

A response is an opaque `usize` handle; **`0` means the request failed** — see
`http_error()`. Bodies are length-carrying, so binary payloads are safe.

| Function | Signature | Description |
|---|---|---|
| `http_get` | `usize http_get(str url)` | GET. |
| `http_post` | `usize http_post(str url, str content_type, str body)` | POST. |
| `http_request` | `usize http_request(str method, str url, array<str> headers, str body)` | Any method; `headers` are `"Name: value"` lines; `body` may be `nil`. |
| `http_status` | `int http_status(usize r)` | Status code. |
| `http_header` | `str http_header(usize r, str name)` | Header value (case-insensitive name), `""` if absent. |
| `http_body` | `str http_body(usize r)` | Body as a `str` copy (text). |
| `http_body_addr` / `http_body_len` | `usize http_body_addr(usize r)` | Body bytes — use these for binary data. |
| `http_save_body` | `bool http_save_body(usize r, str path)` | Write the body to a file. |
| `http_download` | `bool http_download(str url, str path)` | GET and save a `200` response. |
| `http_free` | `void http_free(usize r)` | Release a response. |
| `http_error` | `str http_error()` | Description of the last failure. |
| `url_parse` | `usize url_parse(str url)` | [`map`](map.md) with `scheme`, `host`, `port` (int), `path`; `0` if malformed. |

Lower-level pieces (`http_build_request`, `http_parse_response`, `http_exchange`,
`http_redirect_url`) are public too; `https` reuses them for its TLS transport.

```hylian
include { io, http, }

int main() {
    usize r = http_get("http://example.com/");
    if (r == 0) { eprintln(http_error()); return 1; }
    println(http_body(r));
    http_free(r);
    return 0;
}
```
