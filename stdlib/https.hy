// https — HTTP over TLS, plus a scheme-agnostic fetch.
//
//   r, err := https_get("https://example.com/pkg/index.json");
//   r2, err2 := fetch("http://host/x");     // http:// or https://, either works
//
// Same Response and accessors as `http` (r.status, http_header, http_body,
// r.body / r.body_len, http_free); failures are the Error? half of the
// (Response, Error?) result. This module adds the
// TLS transport (see `tls`: certificates are verified against the system
// trust store, no way to turn that off) and follows redirects across schemes
// (an http URL may redirect to https and vice versa).
//
// Linking this module needs OpenSSL (see std.openssl) - use plain `http` if
// you only talk http:// and don't want that dependency.

include {
    string,
    bytes,
    dns,
    http,
    tls,
}

// _https_exchange: one TLS request/response round trip.
static (Response, Error?) _https_exchange(str host, int port, usize req) {
    str ip = dns_resolve(host);
    if (ip == nil) { return http_fail(concat("could not resolve host: ", host)); }
    usize c = tls_connect(ip, port, host);
    if (c == 0) { return http_fail(tls_error()); }
    if (!tls_send(c, buf_addr(req), buf_len(req))) {
        tls_close(c);
        return http_fail("failed sending request");
    }
    usize rx = buf_new();
    while (true) {
        usize dst = buf_reserve(rx, 16384);
        int n = tls_recv(c, dst, 16384);
        if (n == 0) { break; }
        if (n < 0) {
            tls_close(c);
            buf_free(rx);
            return http_fail("receive failed or timed out");
        }
        buf_commit(rx, n);
    }
    tls_close(c);
    r, err := http_parse_response(buf_addr(rx), buf_len(rx));
    buf_free(rx);
    return (r, err);
}

static (Response, Error?) _exchange(str scheme, str host, int port, usize req) {
    if (equals(scheme, "https")) { return _https_exchange(host, port, req); }
    return http_exchange(host, port, req);
}

// https_request: like http_request, but accepts https:// too and follows
// redirects between the two.
(Response, Error?) https_request(str method, str url, array<str> headers, str body) {
    int hops = 0;
    while (hops <= 5) {
        u, uerr := url_parse(url);
        if (uerr != nil) { return http_fail(uerr.message()); }
        usize req = http_build_request(method, u.host, u.port, u.path, headers, body);
        if (!equals(u.scheme, "https") && !equals(u.scheme, "http")) {
            buf_free(req);
            return http_fail(concat("unsupported URL scheme: ", u.scheme));
        }
        r, err := _exchange(u.scheme, u.host, u.port, req);
        buf_free(req);
        if (err != nil) { return (r, err); }
        str next = http_redirect_url(r, u);
        if (next == nil) { return (r, nil); }
        http_free(r);
        url = next;
        hops += 1;
    }
    return http_fail("too many redirects");
}

// https_get / fetch: GET a http:// or https:// URL.
(Response, Error?) https_get(str url) {
    array<str> none = [];
    return https_request("GET", url, none, nil);
}

(Response, Error?) fetch(str url) {
    return https_get(url);
}

// https_download / fetch_to_file: GET url and save a 200 response body to path.
Error? https_download(str url, str path) {
    r, err := https_get(url);
    if (err != nil) { return err; }
    return http_save_response(r, path);
}
