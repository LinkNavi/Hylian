// net — low-level IPv4/TCP sockets over raw syscalls.
//
// IPv4 only: TCP streams plus UDP datagrams. This module itself takes raw
// dotted-quad IPs; hostname lookup lives in `dns` (dns_resolve) and HTTP in
// `http`, both built on this.

include {
    platform.linux_x86_64,
    string,
    bytes,
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
    // The four octets used to be collected into a raw_alloc(32) block and read
    // back out with hand-written pointer arithmetic, because array<T> had no
    // runtime behind it and every use failed to link. It has one now.
    array<int> octets = [];
    int cur = 0;
    int i = 0;
    int n = length(addr);

    while (i <= n) {
        uint8 c;
        if (i < n) {
            usize p = cast<usize>(addr) + cast<usize>(i);
            unsafe { *uint8 bp = cast<*uint8>(p); c = *bp; }
        } else {
            c = cast<uint8>(46); // treat end-of-string like a trailing '.'
        }

        int ci = cast<int>(c);
        if (ci == 46) { // '.'
            octets.push(cur);
            cur = 0;
        } else {
            cur = cur * 10 + (ci - 48);
        }
        i += 1;
    }

    if (octets.len < 4) { return 0; }
    return octets[0] + octets[1] * 256 + octets[2] * 65536 + octets[3] * 16777216;
}

// _parse_ipv4_valid: true if addr is a well-formed dotted-quad IPv4 address
// (exactly 4 segments, each 1-3 decimal digits, each octet 0-255).
//
// _parse_ipv4 above never checked this — a malformed address (out-of-range
// octet, non-digit characters, wrong segment count) silently produced SOME
// 32-bit value from whatever garbage it parsed, and connect()/bind() would
// try it anyway. E.g. "93.184.21609.34" has an octet (21609) way outside
// 0-255; the old code truncated it into a normal-looking-but-wrong address
// and connect() then blocked until the OS's connect timeout (Linux's default
// is ~127s) trying to reach a host that was never the intended one — which
// looked exactly like an unconditional freeze rather than the fast,
// obvious "bad address" error it should have been.
bool _parse_ipv4_valid(str addr) {
    int cur = 0;
    int cur_digits = 0;
    int octet_count = 0;
    int i = 0;
    int n = length(addr);

    while (i <= n) {
        uint8 c;
        if (i < n) {
            usize p = cast<usize>(addr) + cast<usize>(i);
            unsafe { *uint8 bp = cast<*uint8>(p); c = *bp; }
        } else {
            c = cast<uint8>(46); // treat end-of-string like a trailing '.'
        }

        int ci = cast<int>(c);
        if (ci == 46) { // '.'
            if (cur_digits == 0 || cur_digits > 3 || cur > 255) { return false; }
            octet_count += 1;
            cur = 0;
            cur_digits = 0;
        } else if (ci >= 48 && ci <= 57) { // '0'-'9'
            cur = cur * 10 + (ci - 48);
            cur_digits += 1;
        } else {
            return false; // non-digit, non-dot byte
        }
        i += 1;
    }

    return octet_count == 4;
}

// struct sockaddr_in, exactly as the kernel expects it on Linux/x86-64:
//   offset 0: sin_family (2 bytes)
//   offset 2: sin_port   (2 bytes, network byte order)
//   offset 4: sin_addr   (4 bytes)
//   offset 8: 8 bytes of padding, must be zero
// 16 bytes total. `packed` because this goes straight to the kernel — its
// layout is not ours to pad or reorder.
//
// This replaces a hand-packed raw_alloc(16) that wrote the family and port a
// byte at a time. That code carried a comment explaining that the address had
// to be written in a separate unsafe block because "*int would write 8 bytes
// and stomp into the padding region" — which was a real compiler bug (every
// pointer store was 8 bytes wide regardless of the pointee type) rather than
// anything inherent. With sized stores fixed, a `uint32` field writes 4 bytes,
// so the struct can simply be declared and assigned.
packed class SockAddrIn {
    uint16 family;
    uint16 port;
    uint32 addr;
    uint32 pad_lo;
    uint32 pad_hi;
}

// _fill_sockaddr_in: populate a caller-provided SockAddrIn for ip:port.
static void _fill_sockaddr_in(usize sa_ptr, str ip, int port) {
    unsafe {
        *uint16 fam = cast<*uint16>(sa_ptr);
        *fam = cast<uint16>(AF_INET);
        *uint16 prt = cast<*uint16>(sa_ptr + cast<usize>(2));
        *prt = cast<uint16>(_htons(port));
        *uint32 adr = cast<*uint32>(sa_ptr + cast<usize>(4));
        *adr = cast<uint32>(_parse_ipv4(ip));
        *uint32 p0 = cast<*uint32>(sa_ptr + cast<usize>(8));
        *p0 = cast<uint32>(0);
        *uint32 p1 = cast<*uint32>(sa_ptr + cast<usize>(12));
        *p1 = cast<uint32>(0);
    }
}

// socket: create a TCP/IPv4 socket. Returns a file descriptor or -1.
int tcp_socket() {
    return syscall(NR_SOCKET, AF_INET, SOCK_STREAM, 0);
}

// connect: connect a socket to ip:port. Returns 0 on success, -1 on error.
// Fails fast (no syscall at all) on a malformed `ip` instead of handing the
// kernel a garbage address — see _parse_ipv4_valid's comment.
int connect(int fd, str ip, int port) {
    if (!_parse_ipv4_valid(ip)) { return -1; }
    SockAddrIn sa;
    _fill_sockaddr_in(cast<usize>(&sa), ip, port);
    return syscall(NR_CONNECT, fd, cast<int>(&sa), 16);
}

// bind: bind a socket to ip:port ("0.0.0.0" for all interfaces).
// Returns 0 on success, -1 on error.
int bind(int fd, str ip, int port) {
    if (!_parse_ipv4_valid(ip)) { return -1; }
    SockAddrIn sa;
    _fill_sockaddr_in(cast<usize>(&sa), ip, port);
    return syscall(NR_BIND, fd, cast<int>(&sa), 16);
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

// ── binary-safe send/recv ────────────────────────────────────────────────────
//
// send()/recv() above take `str`, which is fine for text but awkward for
// binary payloads; these take a raw buffer address (see `bytes`).

// send_bytes: send len bytes from buf. Returns bytes sent or a negative errno.
int send_bytes(int fd, usize buf, int len) {
    return syscall(NR_SENDTO, fd, cast<int>(buf), len, 0, 0, 0);
}

// recv_bytes: receive up to len bytes into buf. Returns bytes read, 0 when
// the peer closed the connection, or a negative errno (-11 EAGAIN on timeout).
int recv_bytes(int fd, usize buf, int len) {
    return syscall(NR_RECVFROM, fd, cast<int>(buf), len, 0, 0, 0);
}

// send_all: keep calling send until all len bytes are out. Returns true on
// success, false if the connection failed part way.
bool send_all(int fd, usize buf, int len) {
    int sent = 0;
    while (sent < len) {
        int n = syscall(NR_SENDTO, fd, cast<int>(buf + cast<usize>(sent)), len - sent, 0, 0, 0);
        if (n <= 0) { return false; }
        sent += n;
    }
    return true;
}

// set_timeout: make recv (and connect's replies) on fd give up after
// `seconds` instead of blocking forever - recv then returns -11 (EAGAIN).
// Also applies to send. Returns 0 on success.
int set_timeout(int fd, int seconds) {
    usize tv = bytes_alloc(16); // struct timeval { tv_sec; tv_usec; }
    set64(tv, 0, seconds);
    set64(tv, 8, 0);
    int a = syscall(NR_SETSOCKOPT, fd, 1, 20, cast<int>(tv), 16); // SOL_SOCKET, SO_RCVTIMEO
    int b = syscall(NR_SETSOCKOPT, fd, 1, 21, cast<int>(tv), 16); // SO_SNDTIMEO
    bytes_free(tv, 16);
    if (a != 0) { return a; }
    return b;
}

// ── UDP ──────────────────────────────────────────────────────────────────────

// udp_socket: create a UDP/IPv4 socket. Returns a file descriptor or -1.
int udp_socket() {
    return syscall(NR_SOCKET, AF_INET, SOCK_DGRAM, 0);
}

// sendto: send one datagram of len bytes from buf to ip:port.
// Returns bytes sent, or a negative errno / -1 on a malformed ip.
int sendto(int fd, usize buf, int len, str ip, int port) {
    if (!_parse_ipv4_valid(ip)) { return -1; }
    SockAddrIn sa;
    _fill_sockaddr_in(cast<usize>(&sa), ip, port);
    return syscall(NR_SENDTO, fd, cast<int>(buf), len, 0, cast<int>(&sa), 16);
}

// recvfrom: receive one datagram (up to len bytes) into buf. Returns its
// size or a negative errno (-11 on timeout; see set_timeout). The sender's
// address is not reported.
int recvfrom(int fd, usize buf, int len) {
    return syscall(NR_RECVFROM, fd, cast<int>(buf), len, 0, 0, 0);
}
