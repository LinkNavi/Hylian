#define _POSIX_C_SOURCE 200112L

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// ── Platform abstraction ──────────────────────────────────────────────────────

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  pragma comment(lib, "ws2_32.lib")
   typedef SOCKET hylian_sock_t;
#  define HYLIAN_INVALID_SOCK INVALID_SOCKET
#  define hylian_sock_close(s) closesocket(s)
#  define hylian_sock_errno() WSAGetLastError()
   static int _wsa_init = 0;
   static void _ensure_wsa(void) {
       if (_wsa_init) return;
       WSADATA wd; WSAStartup(MAKEWORD(2,2), &wd); _wsa_init = 1;
   }
#  ifdef _MSC_VER
     typedef SSIZE_T ssize_t;
#  endif
#else
#  include <sys/types.h>
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <netdb.h>
#  include <unistd.h>
#  include <fcntl.h>
   typedef int hylian_sock_t;
#  define HYLIAN_INVALID_SOCK (-1)
#  define hylian_sock_close(s) close(s)
#  define hylian_sock_errno() errno
#  define _ensure_wsa() /* nothing */
#endif

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/opensslv.h>

// ── Internal connection table ─────────────────────────────────────────────────

#define MAX_HTTPS_CONNS 64

typedef struct {
    hylian_sock_t  fd;   /* underlying TCP socket, HYLIAN_INVALID_SOCK = slot free */
    SSL_CTX       *ctx;
    SSL           *ssl;
} HttpsConn;

static HttpsConn _conns[MAX_HTTPS_CONNS];
static int       _conns_init = 0;

static void _init_conns(void) {
    if (_conns_init) return;
    for (int i = 0; i < MAX_HTTPS_CONNS; i++) _conns[i].fd = HYLIAN_INVALID_SOCK;
#if OPENSSL_VERSION_NUMBER < 0x10100000L
    SSL_library_init();
    OpenSSL_add_all_algorithms();
    SSL_load_error_strings();
#endif
    _conns_init = 1;
}

static int _alloc_slot(void) {
    for (int i = 0; i < MAX_HTTPS_CONNS; i++)
        if (_conns[i].fd == HYLIAN_INVALID_SOCK) return i;
    return -1;
}

// ── hylian_net_https_connect ──────────────────────────────────────────────────
//
// Open a TLS connection to host:port. host is NOT null-terminated (host_len
// bytes). verify_peer=1 verifies the server certificate; 0 skips verification.
// Returns a connection handle (>= 0) on success, or -1 on error.

int64_t hylian_net_https_connect(char *host, int64_t host_len,
                                  int64_t port, int64_t verify_peer) {
    if (!host || host_len <= 0 || host_len > 253 || port < 1 || port > 65535)
        return -1;

    _ensure_wsa();
    _init_conns();

    int slot = _alloc_slot();
    if (slot < 0)
        return -1;

    // Build null-terminated copies of host and port for getaddrinfo.
    char host_tmp[254];
    memcpy(host_tmp, host, (size_t)host_len);
    host_tmp[host_len] = '\0';

    char port_tmp[8];
    snprintf(port_tmp, sizeof(port_tmp), "%d", (int)port);

    // Resolve the host and establish a TCP connection.
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *results = NULL;
    if (getaddrinfo(host_tmp, port_tmp, &hints, &results) != 0 || !results)
        return -1;

    hylian_sock_t fd = HYLIAN_INVALID_SOCK;
    for (struct addrinfo *rp = results; rp != NULL; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd == HYLIAN_INVALID_SOCK)
            continue;
        if (connect(fd, rp->ai_addr, (socklen_t)rp->ai_addrlen) == 0)
            break;
        hylian_sock_close(fd);
        fd = HYLIAN_INVALID_SOCK;
    }
    freeaddrinfo(results);

    if (fd == HYLIAN_INVALID_SOCK)
        return -1;

    // Set up TLS over the connected socket.
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
    SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
#else
    SSL_CTX *ctx = SSL_CTX_new(SSLv23_client_method());
#endif
    if (!ctx) {
        hylian_sock_close(fd);
        return -1;
    }

    if (verify_peer) {
        SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);
        SSL_CTX_set_default_verify_paths(ctx);
    } else {
        SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);
    }

    SSL *ssl = SSL_new(ctx);
    if (!ssl) {
        SSL_CTX_free(ctx);
        hylian_sock_close(fd);
        return -1;
    }

    SSL_set_fd(ssl, (int)fd);

    // Set SNI hostname so servers can select the right certificate.
    SSL_set_tlsext_host_name(ssl, host_tmp);

    if (SSL_connect(ssl) != 1) {
        SSL_free(ssl);
        SSL_CTX_free(ctx);
        hylian_sock_close(fd);
        return -1;
    }

    _conns[slot].fd  = fd;
    _conns[slot].ctx = ctx;
    _conns[slot].ssl = ssl;

    return (int64_t)slot;
}

// ── hylian_net_https_send ─────────────────────────────────────────────────────
//
// Send len bytes from buf over the TLS connection identified by handle.
// Returns bytes sent, or -1 on error.

int64_t hylian_net_https_send(int64_t handle, char *buf, int64_t len) {
    if (handle < 0 || handle >= MAX_HTTPS_CONNS || !buf || len <= 0)
        return -1;

    HttpsConn *c = &_conns[(int)handle];
    if (c->fd == HYLIAN_INVALID_SOCK || !c->ssl)
        return -1;

    int n = SSL_write(c->ssl, buf, (int)len);
    if (n <= 0)
        return -1;

    return (int64_t)n;
}

// ── hylian_net_https_recv ─────────────────────────────────────────────────────
//
// Receive up to buf_len bytes into buf from the TLS connection.
// Returns bytes received, 0 on clean close, or -1 on error.

int64_t hylian_net_https_recv(int64_t handle, char *buf, int64_t buf_len) {
    if (handle < 0 || handle >= MAX_HTTPS_CONNS || !buf || buf_len <= 0)
        return -1;

    HttpsConn *c = &_conns[(int)handle];
    if (c->fd == HYLIAN_INVALID_SOCK || !c->ssl)
        return -1;

    int n = SSL_read(c->ssl, buf, (int)buf_len);
    if (n < 0)
        return -1;

    return (int64_t)n;
}

// ── hylian_net_https_close ────────────────────────────────────────────────────
//
// Shut down the TLS connection and release all resources for the given handle.
// Returns 0 on success, or -1 on error.

int64_t hylian_net_https_close(int64_t handle) {
    if (handle < 0 || handle >= MAX_HTTPS_CONNS)
        return -1;

    HttpsConn *c = &_conns[(int)handle];
    if (c->fd == HYLIAN_INVALID_SOCK)
        return -1;

    SSL_shutdown(c->ssl);
    SSL_free(c->ssl);
    SSL_CTX_free(c->ctx);
    hylian_sock_close(c->fd);

    c->ssl = NULL;
    c->ctx = NULL;
    c->fd  = HYLIAN_INVALID_SOCK;

    return 0;
}

// ── hylian_net_https_get ──────────────────────────────────────────────────────
//
// Perform a complete HTTP/1.1 GET request over HTTPS and collect the full
// response (headers + body) into out_buf. The connection is opened and closed
// internally. Returns total bytes received, or -1 on error.

int64_t hylian_net_https_get(char *host, int64_t host_len,
                              char *path, int64_t path_len,
                              char *out_buf, int64_t out_buflen) {
    if (!host || host_len <= 0 || host_len > 253 ||
        !path || path_len <= 0 ||
        !out_buf || out_buflen <= 0)
        return -1;

    // Build null-terminated host and path strings for use in the request.
    char host_tmp[254];
    memcpy(host_tmp, host, (size_t)host_len);
    host_tmp[host_len] = '\0';

    char path_tmp[4096];
    if (path_len >= (int64_t)sizeof(path_tmp))
        return -1;
    memcpy(path_tmp, path, (size_t)path_len);
    path_tmp[path_len] = '\0';

    // Build the HTTP/1.1 GET request.
    char req[8192];
    int req_len = snprintf(req, sizeof(req),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Connection: close\r\n"
        "\r\n",
        path_tmp, host_tmp);

    if (req_len < 0 || req_len >= (int)sizeof(req))
        return -1;

    int64_t handle = hylian_net_https_connect(host, host_len, 443, 1);
    if (handle < 0)
        return -1;

    if (hylian_net_https_send(handle, req, (int64_t)req_len) < 0) {
        hylian_net_https_close(handle);
        return -1;
    }

    int64_t total = 0;
    while (total < out_buflen) {
        int64_t n = hylian_net_https_recv(handle, out_buf + total,
                                           out_buflen - total);
        if (n < 0) {
            hylian_net_https_close(handle);
            return -1;
        }
        if (n == 0)
            break;
        total += n;
    }

    hylian_net_https_close(handle);
    return total;
}

// ── hylian_net_https_post ─────────────────────────────────────────────────────
//
// Perform a complete HTTP/1.1 POST request over HTTPS and collect the full
// response into out_buf. The connection is opened and closed internally.
// Returns total bytes received, or -1 on error.

int64_t hylian_net_https_post(char *host,         int64_t host_len,
                               char *path,         int64_t path_len,
                               char *content_type, int64_t ct_len,
                               char *body,         int64_t body_len,
                               char *out_buf,      int64_t out_buflen) {
    if (!host || host_len <= 0 || host_len > 253 ||
        !path || path_len <= 0 ||
        !content_type || ct_len <= 0 ||
        !out_buf || out_buflen <= 0)
        return -1;

    // Build null-terminated host, path, and content-type strings.
    char host_tmp[254];
    memcpy(host_tmp, host, (size_t)host_len);
    host_tmp[host_len] = '\0';

    char path_tmp[4096];
    if (path_len >= (int64_t)sizeof(path_tmp))
        return -1;
    memcpy(path_tmp, path, (size_t)path_len);
    path_tmp[path_len] = '\0';

    char ct_tmp[256];
    if (ct_len >= (int64_t)sizeof(ct_tmp))
        return -1;
    memcpy(ct_tmp, content_type, (size_t)ct_len);
    ct_tmp[ct_len] = '\0';

    // Build the HTTP/1.1 POST request headers.
    char req_headers[8192];
    int hdr_len = snprintf(req_headers, sizeof(req_headers),
        "POST %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %lld\r\n"
        "Connection: close\r\n"
        "\r\n",
        path_tmp, host_tmp, ct_tmp, (long long)(body_len > 0 ? body_len : 0));

    if (hdr_len < 0 || hdr_len >= (int)sizeof(req_headers))
        return -1;

    int64_t handle = hylian_net_https_connect(host, host_len, 443, 1);
    if (handle < 0)
        return -1;

    // Send headers.
    if (hylian_net_https_send(handle, req_headers, (int64_t)hdr_len) < 0) {
        hylian_net_https_close(handle);
        return -1;
    }

    // Send body (may be NULL or zero-length for bodyless POSTs).
    if (body && body_len > 0) {
        if (hylian_net_https_send(handle, body, body_len) < 0) {
            hylian_net_https_close(handle);
            return -1;
        }
    }

    int64_t total = 0;
    while (total < out_buflen) {
        int64_t n = hylian_net_https_recv(handle, out_buf + total,
                                           out_buflen - total);
        if (n < 0) {
            hylian_net_https_close(handle);
            return -1;
        }
        if (n == 0)
            break;
        total += n;
    }

    hylian_net_https_close(handle);
    return total;
}

// ── hylian_net_https_body ─────────────────────────────────────────────────────
//
// Extract the HTTP response body from a raw response buffer by scanning for
// the \r\n\r\n header/body separator. Writes the body into out_buf.
// Returns the body length on success, or -1 on error.

int64_t hylian_net_https_body(char *response, int64_t response_len,
                               char *out_buf,  int64_t out_buflen) {
    if (!response || response_len <= 0 || !out_buf || out_buflen <= 0)
        return -1;

    // Scan for the blank line that separates headers from body.
    const char *sep = "\r\n\r\n";
    int64_t sep_len = 4;

    for (int64_t i = 0; i <= response_len - sep_len; i++) {
        if (memcmp(response + i, sep, (size_t)sep_len) == 0) {
            int64_t body_start = i + sep_len;
            int64_t body_len   = response_len - body_start;

            if (body_len <= 0)
                return 0;

            int64_t copy_len = body_len < out_buflen ? body_len : out_buflen;
            memcpy(out_buf, response + body_start, (size_t)copy_len);
            return copy_len;
        }
    }

    return -1; // no header/body separator found
}

// ── hylian_net_https_status ───────────────────────────────────────────────────
//
// Parse the HTTP status code from the first line of a raw response buffer.
// Expects the form "HTTP/1.x NNN ...". Returns the status code (e.g. 200),
// or -1 on parse error.

int64_t hylian_net_https_status(char *response, int64_t response_len) {
    if (!response || response_len < 12)
        return -1;

    // The status line must start with "HTTP/".
    if (memcmp(response, "HTTP/", 5) != 0)
        return -1;

    // Find the first space that precedes the status code.
    int64_t i = 5;
    while (i < response_len && response[i] != ' ')
        i++;

    if (i >= response_len)
        return -1;

    i++; // skip the space

    // Need at least 3 digits.
    if (i + 3 > response_len)
        return -1;

    // Validate that the next three characters are ASCII digits.
    for (int64_t j = i; j < i + 3; j++) {
        if (response[j] < '0' || response[j] > '9')
            return -1;
    }

    int64_t status = ((int64_t)(response[i]     - '0') * 100) +
                     ((int64_t)(response[i + 1] - '0') * 10)  +
                     ((int64_t)(response[i + 2] - '0'));

    return status;
}