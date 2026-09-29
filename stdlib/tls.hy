// tls — TLS client connections via the system OpenSSL (libssl).
//
//   usize c = tls_connect("93.184.216.34", 443, "example.com");
//   if (c == 0) { eprintln(tls_error()); }
//   tls_send(c, addr, len);
//   int n = tls_recv(c, addr, cap);      // 0 = closed by peer, <0 = error
//   tls_close(c);
//
// Certificates ARE verified: the chain must validate against the system trust
// store (or $SSL_CERT_FILE / $SSL_CERT_DIR) and the certificate must match
// `host` (a DNS name, or a literal IP address). A failed check fails the
// connection - there is deliberately no "insecure" switch here. The hostname
// is also sent as SNI so virtual-hosted servers present the right cert.
//
// Requires libssl/libcrypto at link time (see std.openssl). The crypto
// itself lives entirely in OpenSSL; this module only drives it.
//
// A connection is an opaque `usize` handle (0 = failure).

include {
    platform.linux_x86_64,
    string,
    bytes,
    net,
    std.openssl,
}

static str _tls_err = "";
static int _tls_ctx = 0;
static int TLS_TIMEOUT_SECS = 20;

// tls_error: why the last tls_connect failed.
str tls_error() {
    return _tls_err;
}

// _i32: sign-extend the low 32 bits of a C `int` return value.
static int _i32(int v) {
    int x = v & 4294967295;
    if (x >= 2147483648) { return x - 4294967296; }
    return x;
}

static usize _tls_fail(str msg) {
    _tls_err = msg;
    return cast<usize>(0);
}

// One shared SSL_CTX for the process (loading the trust store is expensive).
static usize _ctx() {
    if (_tls_ctx != 0) { return cast<usize>(_tls_ctx); }
    usize ctx = SSL_CTX_new(TLS_client_method());
    if (ctx == 0) { return ctx; }
    SSL_CTX_set_verify(ctx, 1, cast<usize>(0)); // SSL_VERIFY_PEER
    // OpenSSL 3 reports a peer that closes without a close_notify as an
    // error; plenty of servers do (Connection: close). Treat it as a normal
    // EOF - http's Content-Length / chunked framing is what detects truncation.
    SSL_CTX_set_options(ctx, cast<usize>(128)); // SSL_OP_IGNORE_UNEXPECTED_EOF
    SSL_CTX_set_default_verify_paths(ctx);
    _tls_ctx = cast<int>(ctx);
    return ctx;
}

// tls_load_ca_file: additionally trust the CA certificates in a PEM file
// (a private registry's CA, for example). Returns true on success.
bool tls_load_ca_file(str path) {
    usize ctx = _ctx();
    if (ctx == 0) { return false; }
    return _i32(SSL_CTX_load_verify_locations(ctx, path, cast<str>(cast<usize>(0)))) == 1;
}

// Connection handle: 2 words { SSL*, fd }.

// tls_connect: TCP-connect to ip:port, then complete a verified TLS handshake
// expecting a certificate for `host`. Returns a handle or 0 (see tls_error()).
usize tls_connect(str ip, int port, str host) {
    usize ctx = _ctx();
    if (ctx == 0) { return _tls_fail("could not initialise OpenSSL"); }
    int fd = tcp_socket();
    if (fd < 0) { return _tls_fail("could not create socket"); }
    set_timeout(fd, TLS_TIMEOUT_SECS);
    if (connect(fd, ip, port) != 0) {
        close(fd);
        return _tls_fail(concat("could not connect to ", ip));
    }
    usize ssl = SSL_new(ctx);
    if (ssl == 0) { close(fd); return _tls_fail("SSL_new failed"); }
    SSL_set_fd(ssl, fd);

    // Certificate name check: IP literal vs DNS name.
    if (_parse_ipv4_valid(host)) {
        X509_VERIFY_PARAM_set1_ip_asc(SSL_get0_param(ssl), host);
    } else {
        SSL_set1_host(ssl, host);
        SSL_ctrl(ssl, 55, 0, cast<usize>(host)); // SSL_CTRL_SET_TLSEXT_HOSTNAME (SNI); type 0 = host_name
    }

    int rc = _i32(SSL_connect(ssl));
    if (rc != 1) {
        int vr = _i32(SSL_get_verify_result(ssl));
        SSL_free(ssl);
        close(fd);
        if (vr != 0) {
            return _tls_fail(concat("certificate verification failed for ", host));
        }
        return _tls_fail("TLS handshake failed");
    }
    usize h = bytes_alloc(16);
    set64(h, 0, cast<int>(ssl));
    set64(h, 8, fd);
    return h;
}

// tls_send: write len bytes; returns true if all were sent.
bool tls_send(usize c, usize buf, int len) {
    usize ssl = cast<usize>(get64(c, 0));
    int sent = 0;
    while (sent < len) {
        int n = _i32(SSL_write(ssl, buf + cast<usize>(sent), len - sent));
        if (n <= 0) { return false; }
        sent += n;
    }
    return true;
}

// tls_recv: read up to cap bytes. Returns the count, 0 if the peer closed
// the connection (cleanly or not), or a negative value on error/timeout.
int tls_recv(usize c, usize buf, int cap) {
    usize ssl = cast<usize>(get64(c, 0));
    int n = _i32(SSL_read(ssl, buf, cap));
    if (n > 0) { return n; }
    int err = _i32(SSL_get_error(ssl, n));
    if (err == 6 || err == 5) { return 0; } // ZERO_RETURN (close_notify) / SYSCALL (abrupt EOF)
    return -1;
}

// tls_close: shut the session down and release the handle.
void tls_close(usize c) {
    usize ssl = cast<usize>(get64(c, 0));
    SSL_shutdown(ssl);
    SSL_free(ssl);
    close(get64(c, 8));
    bytes_free(c, 16);
}
