// net — low-level IPv4/TCP sockets over raw syscalls.
//
// Deliberately minimal for v0.1.0: IPv4 + TCP only, no DNS/getaddrinfo (that
// needs either a resolver library or reading /etc/hosts + a real parser,
// neither of which exists here yet - callers pass a raw dotted-quad IP).

include {
    platform.linux_x86_64,
    string,
}

// _htons: host-to-network byte order for a 16-bit port number (x86-64 is
// little-endian, network byte order is big-endian).
static int _htons(int port) {
    int lo = port % 256;
    int hi = port / 256;
    return lo * 256 + hi;
}

// _parse_ipv4: "a.b.c.d" -> the 4 bytes packed into a 32-bit little-endian
// int the same way struct in_addr expects them (byte order matches memory
// order here, so no additional swap is needed once the bytes are placed).
static int _parse_ipv4(str addr) {
    usize vals = raw_alloc(32); // 4 ints, 8 bytes each is plenty of slack
    int parts = 0;
    int cur = 0;
    int i = 0;
    int n = length(addr);

    while (i <= n) {
        usize p = cast<usize>(addr) + cast<usize>(i);
        uint8 c;
        if (i < n) { unsafe { *uint8 bp = cast<*uint8>(p); c = *bp; } }
        else { c = cast<uint8>(46); } // treat end-of-string like a trailing '.'

        int ci = cast<int>(c);
        if (ci == 46) { // '.'
            if (parts < 4) {
                usize vp = vals + cast<usize>(parts * 8);
                unsafe { *int vip = cast<*int>(vp); *vip = cur; }
            }
            parts += 1;
            cur = 0;
        } else {
            cur = cur * 10 + (ci - 48);
        }
        i += 1;
    }

    int v0; int v1; int v2; int v3;
    unsafe {
        *int p0 = cast<*int>(vals); v0 = *p0;
        *int p1 = cast<*int>(vals + cast<usize>(8)); v1 = *p1;
        *int p2 = cast<*int>(vals + cast<usize>(16)); v2 = *p2;
        *int p3 = cast<*int>(vals + cast<usize>(24)); v3 = *p3;
    }
    raw_free(vals, 32);
    return v0 + v1 * 256 + v2 * 65536 + v3 * 16777216;
}

// _build_sockaddr_in: packs a struct sockaddr_in (16 bytes on Linux x86-64):
//   offset 0: sa_family (2 bytes, AF_INET)
//   offset 2: port      (2 bytes, network byte order)
//   offset 4: addr      (4 bytes)
//   offset 8..16: padding, zeroed
static usize _build_sockaddr_in(str ip, int port) {
    usize buf = raw_alloc(16);
    int family = AF_INET;
    int nport = _htons(port);
    int addr = _parse_ipv4(ip);

    unsafe {
        *uint8 f0 = cast<*uint8>(buf); *f0 = cast<uint8>(family % 256);
        *uint8 f1 = cast<*uint8>(buf + cast<usize>(1)); *f1 = cast<uint8>(family / 256);
        *uint8 p0 = cast<*uint8>(buf + cast<usize>(2)); *p0 = cast<uint8>(nport / 256);
        *uint8 p1 = cast<*uint8>(buf + cast<usize>(3)); *p1 = cast<uint8>(nport % 256);
        *int   a  = cast<*int>(buf + cast<usize>(4));
        // only the low 4 bytes of this int write matter for the address
        // field; the remaining padding bytes are already zero from raw_alloc
    }
    // addr write done separately since *int would write 8 bytes and stomp
    // into the padding region if done as part of the block above
    usize ap = buf + cast<usize>(4);
    unsafe { *int ip4 = cast<*int>(ap); *ip4 = addr; }

    return buf;
}

// socket: create a TCP/IPv4 socket. Returns a file descriptor or -1.
int tcp_socket() {
    return syscall(NR_SOCKET, AF_INET, SOCK_STREAM, 0);
}

// connect: connect a socket to ip:port. Returns 0 on success, -1 on error.
int connect(int fd, str ip, int port) {
    usize addr = _build_sockaddr_in(ip, port);
    int rc = syscall(NR_CONNECT, fd, cast<int>(addr), 16);
    raw_free(addr, 16);
    return rc;
}

// bind: bind a socket to ip:port ("0.0.0.0" for all interfaces).
// Returns 0 on success, -1 on error.
int bind(int fd, str ip, int port) {
    usize addr = _build_sockaddr_in(ip, port);
    int rc = syscall(NR_BIND, fd, cast<int>(addr), 16);
    raw_free(addr, 16);
    return rc;
}

// listen: mark a bound socket as accepting connections.
// Returns 0 on success, -1 on error.
int listen(int fd, int backlog) {
    return syscall(NR_LISTEN, fd, backlog);
}

// accept: accept one incoming connection, returning a new fd for it (or -1).
int accept(int fd) {
    return syscall(NR_ACCEPT, fd, 0, 0);
}

// send: send len bytes from buf on a connected socket.
int send(int fd, str buf, int len) {
    return syscall(NR_SENDTO, fd, cast<int>(buf), len, 0, 0, 0);
}

// recv: receive up to len bytes into buf from a connected socket.
int recv(int fd, str buf, int len) {
    return syscall(NR_RECVFROM, fd, cast<int>(buf), len, 0, 0, 0);
}

// close: close a socket. Same syscall as a regular file descriptor.
void close(int fd) {
    syscall(NR_CLOSE, fd);
}

// shutdown: shut down part or all of a full-duplex socket connection.
// how: 0=read, 1=write, 2=both.
int shutdown(int fd, int how) {
    return syscall(NR_SHUTDOWN, fd, how);
}
