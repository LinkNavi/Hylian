#define _POSIX_C_SOURCE 200112L

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

// ── hylian_net_tcp_connect ────────────────────────────────────────────────────
//
// Connect to a remote host:port. host is NOT null-terminated (host_len bytes).
// Tries each result from getaddrinfo in order (handles IPv4 and IPv6).
// Returns the connected socket fd (>= 0) on success, or -1 on error.

int64_t hylian_net_tcp_connect(char *host, int64_t host_len, int64_t port) {
    if (!host || host_len <= 0 || host_len > 253 || port < 1 || port > 65535)
        return -1;

    // Build a null-terminated copy of the host name.
    char host_tmp[254];
    memcpy(host_tmp, host, (size_t)host_len);
    host_tmp[host_len] = '\0';

    // Convert port number to a string for getaddrinfo.
    char port_tmp[8];
    snprintf(port_tmp, sizeof(port_tmp), "%d", (int)port);

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;     // accept IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *results = NULL;
    if (getaddrinfo(host_tmp, port_tmp, &hints, &results) != 0 || !results)
        return -1;

    int fd = -1;
    for (struct addrinfo *rp = results; rp != NULL; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0)
            continue;

        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0)
            break; // success

        close(fd);
        fd = -1;
    }

    freeaddrinfo(results);
    return (int64_t)fd;
}

// ── hylian_net_tcp_listen ─────────────────────────────────────────────────────
//
// Create a TCP server socket bound to all interfaces on the given port.
// SO_REUSEADDR is always set. backlog controls the pending-connection queue.
// Returns the listener fd (>= 0) on success, or -1 on error.

int64_t hylian_net_tcp_listen(int64_t port, int64_t backlog) {
    if (port < 1 || port > 65535 || backlog < 1)
        return -1;

    int fd = socket(AF_INET6, SOCK_STREAM, 0);
    if (fd < 0) {
        // Fall back to IPv4-only if IPv6 is unavailable.
        fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0)
            return -1;

        int opt = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port        = htons((uint16_t)port);

        if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            close(fd);
            return -1;
        }
    } else {
        // Prefer dual-stack (IPv4 + IPv6) via IPV6_V6ONLY=0.
        int opt = 0;
        setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &opt, sizeof(opt));

        opt = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        struct sockaddr_in6 addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin6_family = AF_INET6;
        addr.sin6_addr   = in6addr_any;
        addr.sin6_port   = htons((uint16_t)port);

        if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            close(fd);
            return -1;
        }
    }

    if (listen(fd, (int)backlog) < 0) {
        close(fd);
        return -1;
    }

    return (int64_t)fd;
}

// ── hylian_net_tcp_accept ─────────────────────────────────────────────────────
//
// Accept the next incoming connection on listener_fd.
// Returns the new client fd (>= 0) on success, or -1 on error.

int64_t hylian_net_tcp_accept(int64_t listener_fd) {
    if (listener_fd < 0)
        return -1;

    int fd = accept((int)listener_fd, NULL, NULL);
    return (int64_t)fd; // accept returns -1 on error already
}

// ── hylian_net_tcp_send ───────────────────────────────────────────────────────
//
// Send len bytes from buf over the socket fd.
// Returns the number of bytes actually sent, or -1 on error.

int64_t hylian_net_tcp_send(int64_t fd, char *buf, int64_t len) {
    if (fd < 0 || !buf || len <= 0)
        return -1;

    ssize_t sent = send((int)fd, buf, (size_t)len, 0);
    return (int64_t)sent;
}

// ── hylian_net_tcp_recv ───────────────────────────────────────────────────────
//
// Receive up to buf_len bytes from socket fd into buf.
// Returns bytes received (> 0), 0 on clean connection close, or -1 on error.

int64_t hylian_net_tcp_recv(int64_t fd, char *buf, int64_t buf_len) {
    if (fd < 0 || !buf || buf_len <= 0)
        return -1;

    ssize_t received = recv((int)fd, buf, (size_t)buf_len, 0);
    return (int64_t)received;
}

// ── hylian_net_tcp_close ──────────────────────────────────────────────────────
//
// Close the socket fd and release its resources.
// Returns 0 on success, or -1 on error.

int64_t hylian_net_tcp_close(int64_t fd) {
    if (fd < 0)
        return -1;

    return (int64_t)close((int)fd);
}

// ── hylian_net_tcp_set_nonblocking ────────────────────────────────────────────
//
// Set the socket to non-blocking (nonblocking=1) or blocking (nonblocking=0)
// mode using fcntl(F_SETFL, O_NONBLOCK).
// Returns 0 on success, or -1 on error.

int64_t hylian_net_tcp_set_nonblocking(int64_t fd, int64_t nonblocking) {
    if (fd < 0)
        return -1;

    int flags = fcntl((int)fd, F_GETFL, 0);
    if (flags < 0)
        return -1;

    if (nonblocking)
        flags |= O_NONBLOCK;
    else
        flags &= ~O_NONBLOCK;

    return (int64_t)fcntl((int)fd, F_SETFL, flags);
}

// ── hylian_net_tcp_set_reuseaddr ──────────────────────────────────────────────
//
// Set (enable=1) or clear (enable=0) the SO_REUSEADDR socket option.
// Returns 0 on success, or -1 on error.

int64_t hylian_net_tcp_set_reuseaddr(int64_t fd, int64_t enable) {
    if (fd < 0)
        return -1;

    int opt = (enable != 0) ? 1 : 0;
    return (int64_t)setsockopt((int)fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
}

// ── hylian_net_tcp_set_timeout ────────────────────────────────────────────────
//
// Set a send or receive timeout on the socket.
//   which: 0 = receive timeout (SO_RCVTIMEO), 1 = send timeout (SO_SNDTIMEO)
//   ms:    timeout in milliseconds (0 disables the timeout)
// Returns 0 on success, or -1 on error.

int64_t hylian_net_tcp_set_timeout(int64_t fd, int64_t which, int64_t ms) {
    if (fd < 0 || (which != 0 && which != 1) || ms < 0)
        return -1;

    struct timeval tv;
    tv.tv_sec  = (time_t)(ms / 1000);
    tv.tv_usec = (suseconds_t)((ms % 1000) * 1000);

    int optname = (which == 0) ? SO_RCVTIMEO : SO_SNDTIMEO;
    return (int64_t)setsockopt((int)fd, SOL_SOCKET, optname, &tv, sizeof(tv));
}