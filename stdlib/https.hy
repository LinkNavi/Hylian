// https — HTTP over TLS, plus a scheme-agnostic fetch.
//
//   usize r = https_get("https://example.com/pkg/index.json");
//   usize r2 = fetch("http://host/x");     // http:// or https://, either works
//
// Same response handle and accessors as `http` (http_status, http_header,
// http_body, http_body_addr/len, http_free, http_error); this module adds the
// TLS transport (see `tls`: certificates are verified against the system
// trust store, no way to turn that off) and follows redirects across schemes
// (an http URL may redirect to https and vice versa).
//
// Linking this module needs OpenSSL (see std.openssl) - use plain `http` if
// you only talk http:// and don't want that dependency.

include {
    string,
    bytes,
    map,
    dns,
    http,
    tls,
}

static usize _hfail(str msg) {
    // http's error string is private to it, so this module keeps its own and
    // reports it from https_error().
    _https_err = msg;
    return cast<usize>(0);
}

static str _https_err = "";

// https_error: description of the most recent failure from this module
// (falls back to http_error() for plain-http failures inside fetch).
str https_error() {
    if (length(_https_err) > 0) { return _https_err; }
    return http_error();
}

// _https_exchange: one TLS request/response round trip.
static usize _https_exchange(str host, int port, usize req) {
    str ip = dns_resolve(host);
    if (ip == nil) { return _hfail(concat("could not resolve host: ", host)); }
    usize c = tls_connect(ip, port, host);
    if (c == 0) { return _hfail(tls_error()); }
    if (!tls_send(c, buf_addr(req), buf_len(req))) {
        tls_close(c);
        return _hfail("failed sending request");
    }
    usize rx = buf_new();
    while (true) {
        usize dst = buf_reserve(rx, 16384);
        int n = tls_recv(c, dst, 16384);
        if (n == 0) { break; }
        if (n < 0) {
            tls_close(c);
            buf_free(rx);
            return _hfail("receive failed or timed out");
        }
        buf_commit(rx, n);
    }
    tls_close(c);
    usize r = http_parse_response(buf_addr(rx), buf_len(rx));
    buf_free(rx);
    if (r == 0) { _https_err = http_error(); }
    return r;
}

// https_request: like http_request, but accepts https:// too and follows
// redirects between the two. Returns a response handle or 0.
usize https_request(str method, str url, array<str> headers, str body) {
    _https_err = "";
    int hops = 0;
    while (hops <= 5) {
        usize u = url_parse(url);
        if (u == 0) { return _hfail(concat("bad URL: ", url)); }
        str scheme = map_get(u, "scheme", "");
        str host = map_get(u, "host", "");
        int port = map_get_int(u, "port", 80);
        usize req = http_build_request(method, host, port, map_get(u, "path", "/"), headers, body);
        usize r = cast<usize>(0);
        if (equals(scheme, "https")) {
            r = _https_exchange(host, port, req);
        } else if (equals(scheme, "http")) {
            r = http_exchange(host, port, req);
            if (r == 0) { _https_err = http_error(); }
        } else {
            buf_free(req);
            return _hfail(concat("unsupported URL scheme: ", scheme));
        }
        buf_free(req);
        if (r == 0) { return r; }
        str next = http_redirect_url(r, u);
        if (next == nil) { return r; }
        http_free(r);
        url = next;
        hops += 1;
    }
    return _hfail("too many redirects");
}

// https_get / fetch: GET a http:// or https:// URL.
usize https_get(str url) {
    array<str> none = [];
    return https_request("GET", url, none, nil);
}

usize fetch(str url) {
    return https_get(url);
}

// https_download / fetch_to_file: GET url and save a 200 response body to path.
bool https_download(str url, str path) {
    usize r = https_get(url);
    if (r == 0) { return false; }
    if (http_status(r) != 200) {
        _https_err = concat("server returned status ", int_to_str_(http_status(r)));
        http_free(r);
        return false;
    }
    bool ok = http_save_body(r, path);
    if (!ok) { _https_err = concat("could not write ", path); }
    http_free(r);
    return ok;
}

static str int_to_str_(int v) {
    array<str> parts = [];
    if (v == 0) { return "0"; }
    str tbl = "0123456789";
    while (v > 0) { parts.push(slice(tbl, v % 10, v % 10 + 1)); v = v / 10; }
    array<str> rev = [];
    int i = parts.len - 1;
    while (i >= 0) { rev.push(parts[i]); i -= 1; }
    return join(rev, "");
}
