// http — an HTTP/1.1 client over plain TCP.
//
//   usize r = http_get("http://example.com/index.html");
//   if (r == 0) { eprintln(http_error()); }
//   else {
//       int status = http_status(r);              // 200
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
// A response is an opaque `usize` handle (0 means the request failed - see
// http_error()). Bodies are length-carrying, so binary payloads (tarballs) are
// safe: use http_body_addr / http_body_len for those, http_body for text.
// Response memory is not freed automatically; call http_free when done.
//
// Header names are case-insensitive on lookup. Repeated headers keep the last
// value.

include {
    platform.linux_x86_64,
    string,
    bytes,
    map,
    net,
    dns,
}

static str _http_err = "";
static int HTTP_TIMEOUT_SECS = 20;
static int HTTP_MAX_REDIRECTS = 5;

// http_error: description of the most recent failure ("" if none).
str http_error() {
    return _http_err;
}

static usize _fail(str msg) {
    _http_err = msg;
    return cast<usize>(0);
}

// ── URLs ─────────────────────────────────────────────────────────────────────

// url_parse: split "scheme://host[:port][/path]" into a map with the keys
// "scheme", "host", "port" (int; 80/443 defaults) and "path" (at least "/",
// query string included). Returns 0 if url has no "scheme://" prefix or an
// empty host.
usize url_parse(str url) {
    int sep = index_of(url, "://");
    if (sep <= 0) { return cast<usize>(0); }
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
    if (length(host) == 0) { return cast<usize>(0); }

    usize m = map_new();
    map_put(m, "scheme", scheme);
    map_put(m, "host", host);
    map_put_int(m, "port", port);
    map_put(m, "path", path);
    return m;
}

// ── response handle ──────────────────────────────────────────────────────────
// 4 words: status, headers map, body address, body length.

int http_status(usize r) { return get64(r, 0); }
usize http_body_addr(usize r) { return cast<usize>(get64(r, 16)); }
int http_body_len(usize r) { return get64(r, 24); }

// http_header: value of the named header (case-insensitive), or "" if absent.
str http_header(usize r, str name) {
    return map_get(cast<usize>(get64(r, 8)), to_lower(name), "");
}

// http_body: the body as a NUL-terminated str copy (text; a body containing
// NUL bytes will look truncated to str functions - use http_body_addr).
str http_body(usize r) {
    return bytes_to_str(http_body_addr(r), 0, http_body_len(r));
}

// http_free: release a response and everything it owns.
void http_free(usize r) {
    bytes_free(http_body_addr(r), http_body_len(r));
    raw_free(r, 32);
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
// until the connection closed) into a response handle, or 0 (with
// http_error set) if they don't hold a complete, well-formed response.
usize http_parse_response(usize p, int len) {
    int hend = _find_header_end(p, len);
    if (hend < 0) { return _fail("incomplete response headers"); }
    array<str> lines = split(bytes_to_str(p, 0, hend), "\r\n");
    array<str> status_line = split(lines[0], " ");
    if (status_line.len < 2 || !starts_with(status_line[0], "HTTP/")) {
        return _fail("malformed status line");
    }
    int status = parse_int(status_line[1], -1);
    if (status < 0) { return _fail("malformed status code"); }

    usize headers = map_new();
    int i = 1;
    while (i < lines.len) {
        int colon = index_of(lines[i], ":");
        if (colon > 0) {
            str name = to_lower(trim(slice(lines[i], 0, colon)));
            map_put(headers, name, trim(slice(lines[i], colon + 1, length(lines[i]))));
        }
        i += 1;
    }

    int body_off = hend + 4;
    int avail = len - body_off;
    usize body = buf_new();
    if (equals(to_lower(map_get(headers, "transfer-encoding", "")), "chunked")) {
        if (!_decode_chunked(p, body_off, len, body)) {
            buf_free(body);
            return _fail("malformed chunked body");
        }
    } else {
        int want = parse_int(map_get(headers, "content-length", ""), -1);
        if (want >= 0 && want > avail) {
            buf_free(body);
            return _fail("connection closed before the full body arrived");
        }
        if (want < 0) { want = avail; }
        buf_append(body, p, body_off, want);
    }

    usize r = raw_alloc(32);
    set64(r, 0, status);
    set64(r, 8, cast<int>(headers));
    // Hand the buffer's storage to the response, sized exactly to the body so
    // http_free can return it. Trim by copying into an exact-size block.
    int blen = buf_len(body);
    usize exact = bytes_alloc(blen);
    bytes_copy(exact, 0, buf_addr(body), 0, blen);
    buf_free(body);
    set64(r, 16, cast<int>(exact));
    set64(r, 24, blen);
    return r;
}

// http_redirect_url: if r is a redirect with a usable Location header, the
// absolute URL to follow (scheme/host/port taken from `u` for "/path"
// locations); otherwise nil.
str http_redirect_url(usize r, usize u) {
    int s = http_status(r);
    if (s != 301 && s != 302 && s != 303 && s != 307 && s != 308) { return nil; }
    str loc = http_header(r, "location");
    if (length(loc) == 0) { return nil; }
    if (index_of(loc, "://") > 0) { return loc; }
    if (starts_with(loc, "/")) {
        str scheme = map_get(u, "scheme", "http");
        int port = map_get_int(u, "port", 80);
        str hp = map_get(u, "host", "");
        if (!(equals(scheme, "http") && port == 80) && !(equals(scheme, "https") && port == 443)) {
            hp = concat(concat(hp, ":"), _itoa(port));
        }
        return concat(concat(concat(scheme, "://"), hp), loc);
    }
    return nil;
}

// ── transport ────────────────────────────────────────────────────────────────

// http_exchange: connect to host:port over plain TCP, send `req` (a Buffer),
// read until the server closes, and parse the response. Returns 0 on failure.
usize http_exchange(str host, int port, usize req) {
    str ip = dns_resolve(host);
    if (ip == nil) { return _fail(concat("could not resolve host: ", host)); }
    int fd = tcp_socket();
    if (fd < 0) { return _fail("could not create socket"); }
    set_timeout(fd, HTTP_TIMEOUT_SECS);
    if (connect(fd, ip, port) != 0) {
        close(fd);
        return _fail(concat("could not connect to ", ip));
    }
    if (!send_all(fd, buf_addr(req), buf_len(req))) {
        close(fd);
        return _fail("failed sending request");
    }
    usize rx = buf_new();
    while (true) {
        usize dst = buf_reserve(rx, 16384);
        int n = recv_bytes(fd, dst, 16384);
        if (n == 0) { break; }
        if (n < 0) {
            close(fd);
            buf_free(rx);
            return _fail("receive failed or timed out");
        }
        buf_commit(rx, n);
    }
    close(fd);
    usize r = http_parse_response(buf_addr(rx), buf_len(rx));
    buf_free(rx);
    return r;
}

// http_request: send `method` to `url` (http:// only) with extra headers and
// an optional body (nil for none), following redirects. Returns a response
// handle, or 0 (see http_error()).
usize http_request(str method, str url, array<str> headers, str body) {
    int hops = 0;
    while (hops <= HTTP_MAX_REDIRECTS) {
        usize u = url_parse(url);
        if (u == 0) { return _fail(concat("bad URL: ", url)); }
        str scheme = map_get(u, "scheme", "");
        if (!equals(scheme, "http")) {
            return _fail(concat("http can't fetch this scheme (use https for https://): ", scheme));
        }
        str host = map_get(u, "host", "");
        int port = map_get_int(u, "port", 80);
        usize req = http_build_request(method, host, port, map_get(u, "path", "/"), headers, body);
        usize r = http_exchange(host, port, req);
        buf_free(req);
        if (r == 0) { return r; }
        str next = http_redirect_url(r, u);
        if (next == nil) { return r; }
        http_free(r);
        url = next;
        hops += 1;
    }
    return _fail("too many redirects");
}

// http_get: GET url. See http_request.
usize http_get(str url) {
    array<str> none = [];
    return http_request("GET", url, none, nil);
}

// http_post: POST body to url with the given Content-Type.
usize http_post(str url, str content_type, str body) {
    array<str> h = [];
    h.push(concat("Content-Type: ", content_type));
    return http_request("POST", url, h, body);
}

// http_save_body: write a response's body to `path` (created/truncated).
// Returns true if every byte was written.
bool http_save_body(usize r, str path) {
    int fd = syscall(NR_OPEN, cast<int>(path), O_WRONLY + O_CREAT + O_TRUNC, 420);
    if (fd < 0) { return false; }
    int len = http_body_len(r);
    int done = 0;
    while (done < len) {
        int n = syscall(NR_WRITE, fd, cast<int>(http_body_addr(r) + cast<usize>(done)), len - done);
        if (n <= 0) { break; }
        done += n;
    }
    syscall(NR_CLOSE, fd);
    return done == len;
}

// http_download: GET url and save a 200 response's body to `path`.
// Returns true on success; on failure http_error() says why.
bool http_download(str url, str path) {
    usize r = http_get(url);
    if (r == 0) { return false; }
    if (http_status(r) != 200) {
        _http_err = concat("server returned status ", _itoa(http_status(r)));
        http_free(r);
        return false;
    }
    bool ok = http_save_body(r, path);
    if (!ok) { _http_err = concat("could not write ", path); }
    http_free(r);
    return ok;
}
