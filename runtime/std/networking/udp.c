#define _POSIX_C_SOURCE 200112L
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>

/* Create a UDP socket. Returns fd or -1. */
int64_t hylian_net_udp_socket(void) {
    int fd = socket(AF_INET6, SOCK_DGRAM, 0);
    if (fd < 0) {
        /* Fall back to IPv4-only if IPv6 is unavailable. */
        fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (fd < 0) return -1;
        return (int64_t)fd;
    }

    /* Allow IPv4-mapped addresses on the IPv6 socket. */
    int off = 0;
    setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof(off));
    return (int64_t)fd;
}

/* Bind a UDP socket to a local port (all interfaces). Returns 0 or -1. */
int64_t hylian_net_udp_bind(int64_t fd, int64_t port) {
    struct sockaddr_in6 addr6;
    memset(&addr6, 0, sizeof(addr6));
    addr6.sin6_family = AF_INET6;
    addr6.sin6_addr   = in6addr_any;
    addr6.sin6_port   = htons((uint16_t)port);

    if (bind((int)fd, (struct sockaddr *)&addr6, sizeof(addr6)) == 0)
        return 0;

    /* Retry with IPv4 if the socket is IPv4-only. */
    struct sockaddr_in addr4;
    memset(&addr4, 0, sizeof(addr4));
    addr4.sin_family      = AF_INET;
    addr4.sin_addr.s_addr = INADDR_ANY;
    addr4.sin_port        = htons((uint16_t)port);

    if (bind((int)fd, (struct sockaddr *)&addr4, sizeof(addr4)) == 0)
        return 0;

    return -1;
}

/*
 * Resolve host (non-null-terminated, host_len bytes) to a getaddrinfo result.
 * The caller must freeaddrinfo() the returned pointer.
 * Returns NULL on failure.
 */
static struct addrinfo *resolve_host(char *host, int64_t host_len, int64_t port) {
    /* Copy host into a null-terminated buffer for getaddrinfo. */
    char *host_z = malloc((size_t)host_len + 1);
    if (!host_z) return NULL;
    memcpy(host_z, host, (size_t)host_len);
    host_z[host_len] = '\0';

    /* Convert port to string. */
    char port_str[24];
    snprintf(port_str, sizeof(port_str), "%d", (int)port);

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;

    struct addrinfo *res = NULL;
    int rc = getaddrinfo(host_z, port_str, &hints, &res);
    free(host_z);
    if (rc != 0) return NULL;
    return res;
}

/*
 * Send len bytes from buf to host:port via fd.
 * host is NOT null-terminated (host_len bytes).
 * Returns bytes sent or -1.
 */
int64_t hylian_net_udp_send_to(int64_t fd, char *host, int64_t host_len,
                                int64_t port, char *buf, int64_t len) {
    struct addrinfo *res = resolve_host(host, host_len, port);
    if (!res) return -1;

    ssize_t sent = sendto((int)fd, buf, (size_t)len, 0,
                          res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);
    if (sent < 0) return -1;
    return (int64_t)sent;
}

/*
 * Receive a datagram into buf (up to buf_len bytes).
 * Writes sender's IP string (null-terminated) into src_addr_buf (at least 46 bytes).
 * Writes sender's port into *src_port_out.
 * Returns bytes received or -1.
 */
int64_t hylian_net_udp_recv_from(int64_t fd, char *buf, int64_t buf_len,
                                  char *src_addr_buf, int64_t src_addr_buflen,
                                  int64_t *src_port_out) {
    struct sockaddr_storage src;
    socklen_t src_len = sizeof(src);

    ssize_t received = recvfrom((int)fd, buf, (size_t)buf_len, 0,
                                (struct sockaddr *)&src, &src_len);
    if (received < 0) return -1;

    /* Extract IP string and port from the sender address. */
    if (src.ss_family == AF_INET) {
        struct sockaddr_in *s4 = (struct sockaddr_in *)&src;
        inet_ntop(AF_INET, &s4->sin_addr, src_addr_buf, (socklen_t)src_addr_buflen);
        if (src_port_out) *src_port_out = (int64_t)ntohs(s4->sin_port);
    } else if (src.ss_family == AF_INET6) {
        struct sockaddr_in6 *s6 = (struct sockaddr_in6 *)&src;
        /* Unwrap IPv4-mapped addresses (::ffff:a.b.c.d) for readability. */
        if (IN6_IS_ADDR_V4MAPPED(&s6->sin6_addr)) {
            struct in_addr v4;
            memcpy(&v4, s6->sin6_addr.s6_addr + 12, sizeof(v4));
            inet_ntop(AF_INET, &v4, src_addr_buf, (socklen_t)src_addr_buflen);
        } else {
            inet_ntop(AF_INET6, &s6->sin6_addr, src_addr_buf, (socklen_t)src_addr_buflen);
        }
        if (src_port_out) *src_port_out = (int64_t)ntohs(s6->sin6_port);
    } else {
        if (src_addr_buflen > 0) src_addr_buf[0] = '\0';
        if (src_port_out) *src_port_out = 0;
    }

    return (int64_t)received;
}

/*
 * "Connect" a UDP socket to a fixed remote address.
 * host is NOT null-terminated (host_len bytes).
 * Returns 0 or -1.
 */
int64_t hylian_net_udp_connect(int64_t fd, char *host, int64_t host_len, int64_t port) {
    struct addrinfo *res = resolve_host(host, host_len, port);
    if (!res) return -1;

    int rc = connect((int)fd, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);
    return (rc == 0) ? 0 : -1;
}

/* Send on a "connected" UDP socket. Returns bytes sent or -1. */
int64_t hylian_net_udp_send(int64_t fd, char *buf, int64_t len) {
    ssize_t sent = send((int)fd, buf, (size_t)len, 0);
    if (sent < 0) return -1;
    return (int64_t)sent;
}

/* Receive on a "connected" UDP socket. Returns bytes received or -1. */
int64_t hylian_net_udp_recv(int64_t fd, char *buf, int64_t buf_len) {
    ssize_t received = recv((int)fd, buf, (size_t)buf_len, 0);
    if (received < 0) return -1;
    return (int64_t)received;
}

/* Close a UDP socket. Returns 0 or -1. */
int64_t hylian_net_udp_close(int64_t fd) {
    return (close((int)fd) == 0) ? 0 : -1;
}

/* Set socket to non-blocking. nonblocking=1 to enable, 0 to disable. Returns 0 or -1. */
int64_t hylian_net_udp_set_nonblocking(int64_t fd, int64_t nonblocking) {
    int flags = fcntl((int)fd, F_GETFL, 0);
    if (flags < 0) return -1;

    if (nonblocking)
        flags |= O_NONBLOCK;
    else
        flags &= ~O_NONBLOCK;

    return (fcntl((int)fd, F_SETFL, flags) == 0) ? 0 : -1;
}

/*
 * Set recv/send timeout in milliseconds.
 * which: 0 = recv timeout (SO_RCVTIMEO), 1 = send timeout (SO_SNDTIMEO).
 * Returns 0 or -1.
 */
int64_t hylian_net_udp_set_timeout(int64_t fd, int64_t which, int64_t ms) {
    struct timeval tv;
    tv.tv_sec  = (time_t)(ms / 1000);
    tv.tv_usec = (suseconds_t)((ms % 1000) * 1000);

    int optname = (which == 0) ? SO_RCVTIMEO : SO_SNDTIMEO;
    int rc = setsockopt((int)fd, SOL_SOCKET, optname, &tv, sizeof(tv));
    return (rc == 0) ? 0 : -1;
}

/* Enable/disable SO_BROADCAST. Returns 0 or -1. */
int64_t hylian_net_udp_set_broadcast(int64_t fd, int64_t enable) {
    int val = (enable != 0) ? 1 : 0;
    int rc  = setsockopt((int)fd, SOL_SOCKET, SO_BROADCAST, &val, sizeof(val));
    return (rc == 0) ? 0 : -1;
}

/*
 * Join an IPv4 multicast group.
 * group_addr is the multicast IP string (non-null-terminated, group_addr_len bytes).
 * Returns 0 or -1.
 */
int64_t hylian_net_udp_join_multicast(int64_t fd, char *group_addr, int64_t group_addr_len) {
    char *addr_z = malloc((size_t)group_addr_len + 1);
    if (!addr_z) return -1;
    memcpy(addr_z, group_addr, (size_t)group_addr_len);
    addr_z[group_addr_len] = '\0';

    struct ip_mreq mreq;
    memset(&mreq, 0, sizeof(mreq));

    if (inet_pton(AF_INET, addr_z, &mreq.imr_multiaddr) != 1) {
        free(addr_z);
        return -1;
    }
    free(addr_z);

    mreq.imr_interface.s_addr = INADDR_ANY;

    int rc = setsockopt((int)fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));
    return (rc == 0) ? 0 : -1;
}