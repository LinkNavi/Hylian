// http — an HTTP/1.1 client over plain TCP.
//
//   r, err := http_get("http://example.com/index.html");
//   if (err != nil) { eprintln(err.message()); }
//   else {
//       int status = r.status;                    // 200
//       str type = http_header(r, "Content-Type");
//       str body = http_body(r);                  // text
//   }
//
// Handles: hostname lookup (see `dns`), Content-Length, chunked
// transfer-encoding, read-until-close bodies, and redirects (301/302/303/307/
// 308, up to 5 hops, absolute or same-host "/path" Locations).
//
// http:// ONLY. There is no TLS in here: an https:// URL fails with a clear
// error from http_get. Use `https` (https_get / fetch), which speaks TLS
// through the system OpenSSL, for those.
//
// A Response is a struct: status, parallel header name/value arrays (names
// lower-cased), and the body as an address + length. Bodies are
// length-carrying, so binary payloads (tarballs) are safe: use r.body /
// r.body_len for those, http_body for text. Failures come back as the
// Error? half of the (Response, Error?) tuple.
// Response memory is not freed automatically; call http_free when done.
//
// Header names are case-insensitive on lookup. Repeated headers keep the last
// value.

include {
    platform.linux_x86_64,
    string,
    bytes,
    net,
    dns,
}

static int HTTP_TIMEOUT_SECS = 20;
static int HTTP_MAX_REDIRECTS = 5;

struct Response {
    int status;
    array<str> names;
    array<str> values;
    usize body;
    int body_len;
}

struct Url {
    str scheme;
    str host;
    int port;
    str path;
}

// http_fail: an error result, for modules built on http.
(Response, Error?) http_fail(str msg) {
    array<str> none = [];
    Response r = Response { status: 0, names: none, values: none, body: cast<usize>(0), body_len: 0 };
    return (r, Err(msg, 0));
}

static (Url, Error?) _urlhttp_fail(str msg) {
    Url u = Url { scheme: "", host: "", port: 0, path: "" };
    return (u, Err(msg, 0));
}

// ── URLs ─────────────────────────────────────────────────────────────────────

// url_parse: split "scheme://host[:port][/path]" into a Url (port defaults to
// 80/443, path to "/", query string included). Errors if url has no
// "scheme://" prefix or an empty host.
(Url, Error?) url_parse(str url) {
    int sep = index_of(url, "://");
    if (sep <= 0) { return _urlhttp_fail(concat("bad URL: ", url)); }
    str scheme = to_lower(slice(url, 0, sep));
    str rest = slice(url, sep + 3, length(url));

    int slash = index_of(rest, "/");
    str hostport = rest;
    str path = "/";
    if (slash >= 0) {
        hostport = slice(rest, 0, slash);
        path = slice(rest, slash, length(rest));
    }
    int port = 80;
    if (equals(scheme, "https")) { port = 443; }
    str host = hostport;
    int colon = last_index_of(hostport, ":");
    if (colon >= 0) {
        host = slice(hostport, 0, colon);
        port = parse_int(slice(hostport, colon + 1, length(hostport)), port);
    }
    if (length(host) == 0) { return _urlhttp_fail(concat("bad URL: ", url)); }
    return (Url { scheme: scheme, host: host, port: port, path: path }, nil);
}

// ── response ─────────────────────────────────────────────────────────────────

// http_header: value of the named header (case-insensitive), or "" if absent.
str http_header(Response r, str name) {
    str n = to_lower(name);
    int i = r.names.len - 1; // repeated headers keep the last value
    while (i >= 0) {
        if (equals(r.names[i], n)) { return r.values[i]; }
        i -= 1;
    }
    return "";
}

// http_body: the body as a NUL-terminated str copy (text; a body containing
// NUL bytes will look truncated to str functions - use http_body_addr).
str http_body(Response r) {
    return bytes_to_str(r.body, 0, r.body_len);
}

// http_free: release a response and everything it owns.
void http_free(Response r) {
    bytes_free(r.body, r.body_len);
}

// ── request building ─────────────────────────────────────────────────────────

// http_build_request: the raw bytes of a request, as a Buffer (see bytes).
// `headers` holds extra "Name: value" lines (may be empty); `body` may be nil.
// Host, User-Agent, Accept, Connection: close, and Content-Length (when there
// is a body) are added automatically.
usize http_build_request(str method, str host, int port, str path, array<str> headers, str body) {
    usize b = buf_new();
    buf_append_str(b, method);
    buf_append_str(b, " ");
    buf_append_str(b, path);
    buf_append_str(b, " HTTP/1.1\r\nHost: ");
    buf_append_str(b, host);
    if (port != 80 && port != 443) {
        buf_append_str(b, ":");
        buf_append_str(b, _itoa(port));
    }
    buf_append_str(b, "\r\nUser-Agent: hylian-http/1.0\r\nAccept: */*\r\nConnection: close\r\n");
    int i = 0;
    while (i < headers.len) {
        buf_append_str(b, headers[i]);
        buf_append_str(b, "\r\n");
        i += 1;
    }
    if (body != nil) {
        buf_append_str(b, "Content-Length: ");
        buf_append_str(b, _itoa(length(body)));
        buf_append_str(b, "\r\n");
    }
    buf_append_str(b, "\r\n");
    if (body != nil) { buf_append_str(b, body); }
    return b;
}

// _itoa: decimal text of a non-negative int (kept local so http doesn't
// depend on io, whose print functions most callers include separately).
static str _itoa(int v) {
    if (v <= 0) { return "0"; }
    array<str> digits = [];
    str tbl = "0123456789";
    while (v > 0) {
        digits.push(slice(tbl, v % 10, v % 10 + 1));
        v = v / 10;
    }
    array<str> rev = [];
    int i = digits.len - 1;
    while (i >= 0) { rev.push(digits[i]); i -= 1; }
    return join(rev, "");
}

// ── response parsing ─────────────────────────────────────────────────────────

// _find_header_end: offset of the first "\r\n\r\n" in the buffer, or -1.
static int _find_header_end(usize p, int len) {
    int i = 0;
    while (i + 3 < len) {
        if (get8(p, i) == 13 && get8(p, i + 1) == 10 && get8(p, i + 2) == 13 && get8(p, i + 3) == 10) {
            return i;
        }
        i += 1;
    }
    return -1;
}

// _decode_chunked: the de-chunked payload of the chunked stream at p+off..len,
// appended to `out`. Returns false on malformed framing.
static bool _decode_chunked(usize p, int off, int len, usize out) {
    while (off < len) {
        int eol = off;
        while (eol + 1 < len && !(get8(p, eol) == 13 && get8(p, eol + 1) == 10)) { eol += 1; }
        if (eol + 1 >= len) { return false; }
        str line = bytes_to_str(p, off, eol - off);
        int semi = index_of(line, ";"); // ignore chunk extensions
        if (semi >= 0) { line = slice(line, 0, semi); }
        int size = parse_hex(trim(line), -1);
        if (size < 0) { return false; }
        off = eol + 2;
        if (size == 0) { return true; }
        if (off + size > len) { return false; }
        buf_append(out, p, off, size);
        off += size + 2; // data + trailing CRLF
    }
    return false;
}

// http_parse_response: turn the raw bytes received (headers + body, read
// until the connection closed) into a Response, or an error if they don't
// hold a complete, well-formed response.
(Response, Error?) http_parse_response(usize p, int len) {
    int hend = _find_header_end(p, len);
    if (hend < 0) { return http_fail("incomplete response headers"); }
    array<str> lines = split(bytes_to_str(p, 0, hend), "\r\n");
    array<str> status_line = split(lines[0], " ");
    if (status_line.len < 2 || !starts_with(status_line[0], "HTTP/")) {
        return http_fail("malformed status line");
    }
    int status = parse_int(status_line[1], -1);
    if (status < 0) { return http_fail("malformed status code"); }

    array<str> names = [];
    array<str> values = [];
    int i = 1;
    while (i < lines.len) {
        int colon = index_of(lines[i], ":");
        if (colon > 0) {
            names.push(to_lower(trim(slice(lines[i], 0, colon))));
            values.push(trim(slice(lines[i], colon + 1, length(lines[i]))));
        }
        i += 1;
    }
    Response r = Response { status: status, names: names, values: values, body: cast<usize>(0), body_len: 0 };

    int body_off = hend + 4;
    int avail = len - body_off;
    usize body = buf_new();
    if (equals(to_lower(http_header(r, "transfer-encoding")), "chunked")) {
        if (!_decode_chunked(p, body_off, len, body)) {
            buf_free(body);
            return http_fail("malformed chunked body");
        }
    } else {
        int want = parse_int(http_header(r, "content-length"), -1);
        if (want >= 0 && want > avail) {
            buf_free(body);
            return http_fail("connection closed before the full body arrived");
        }
        if (want < 0) { want = avail; }
        buf_append(body, p, body_off, want);
    }

    // Trim by copying into an exact-size block so http_free can return it.
    int blen = buf_len(body);
    usize exact = bytes_alloc(blen);
    bytes_copy(exact, 0, buf_addr(body), 0, blen);
    buf_free(body);
    r.body = exact;
    r.body_len = blen;
    return (r, nil);
}

// http_redirect_url: if r is a redirect with a usable Location header, the
// absolute URL to follow (scheme/host/port taken from `u` for "/path"
// locations); otherwise nil.
str http_redirect_url(Response r, Url u) {
    int s = r.status;
    if (s != 301 && s != 302 && s != 303 && s != 307 && s != 308) { return nil; }
    str loc = http_header(r, "location");
    if (length(loc) == 0) { return nil; }
    if (index_of(loc, "://") > 0) { return loc; }
    if (starts_with(loc, "/")) {
        str hp = u.host;
        if (!(equals(u.scheme, "http") && u.port == 80) && !(equals(u.scheme, "https") && u.port == 443)) {
            hp = concat(concat(hp, ":"), _itoa(u.port));
        }
        return concat(concat(concat(u.scheme, "://"), hp), loc);
    }
    return nil;
}

// ── transport ────────────────────────────────────────────────────────────────

// http_exchange: connect to host:port over plain TCP, send `req` (a Buffer),
// read until the server closes, and parse the response.
(Response, Error?) http_exchange(str host, int port, usize req) {
    str ip = dns_resolve(host);
    if (ip == nil) { return http_fail(concat("could not resolve host: ", host)); }
    int fd = tcp_socket();
    if (fd < 0) { return http_fail("could not create socket"); }
    set_timeout(fd, HTTP_TIMEOUT_SECS);
    if (connect(fd, ip, port) != 0) {
        close(fd);
        return http_fail(concat("could not connect to ", ip));
    }
    if (!send_all(fd, buf_addr(req), buf_len(req))) {
        close(fd);
        return http_fail("failed sending request");
    }
    usize rx = buf_new();
    while (true) {
        usize dst = buf_reserve(rx, 16384);
        int n = recv_bytes(fd, dst, 16384);
        if (n == 0) { break; }
        if (n < 0) {
            close(fd);
            buf_free(rx);
            return http_fail("receive failed or timed out");
        }
        buf_commit(rx, n);
    }
    close(fd);
    r, err := http_parse_response(buf_addr(rx), buf_len(rx));
    buf_free(rx);
    return (r, err);
}

// http_request: send `method` to `url` (http:// only) with extra headers and
// an optional body (nil for none), following redirects.
(Response, Error?) http_request(str method, str url, array<str> headers, str body) {
    int hops = 0;
    while (hops <= HTTP_MAX_REDIRECTS) {
        u, uerr := url_parse(url);
        if (uerr != nil) { return http_fail(uerr.message()); }
        if (!equals(u.scheme, "http")) {
            return http_fail(concat("http can't fetch this scheme (use https for https://): ", u.scheme));
        }
        usize req = http_build_request(method, u.host, u.port, u.path, headers, body);
        r, err := http_exchange(u.host, u.port, req);
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

// http_get: GET url. See http_request.
(Response, Error?) http_get(str url) {
    array<str> none = [];
    return http_request("GET", url, none, nil);
}

// http_post: POST body to url with the given Content-Type.
(Response, Error?) http_post(str url, str content_type, str body) {
    array<str> h = [];
    h.push(concat("Content-Type: ", content_type));
    return http_request("POST", url, h, body);
}

// http_save_body: write a response's body to `path` (created/truncated).
// Returns true if every byte was written.
bool http_save_body(Response r, str path) {
    int fd = syscall(NR_OPEN, cast<int>(path), O_WRONLY + O_CREAT + O_TRUNC, 420);
    if (fd < 0) { return false; }
    int done = 0;
    while (done < r.body_len) {
        int n = syscall(NR_WRITE, fd, cast<int>(r.body + cast<usize>(done)), r.body_len - done);
        if (n <= 0) { break; }
        done += n;
    }
    syscall(NR_CLOSE, fd);
    return done == r.body_len;
}

// http_save_response: save a 200 response's body to `path`, then free it.
Error? http_save_response(Response r, str path) {
    int status = r.status;
    bool ok = status == 200 && http_save_body(r, path);
    http_free(r);
    if (status != 200) { return Err(concat("server returned status ", _itoa(status)), status); }
    if (!ok) { return Err(concat("could not write ", path), 0); }
    return nil;
}

// http_download: GET url and save a 200 response's body to `path`.
Error? http_download(str url, str path) {
    r, err := http_get(url);
    if (err != nil) { return err; }
    return http_save_response(r, path);
}
