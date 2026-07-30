# `net`

```hylian
include { net, }
```

Source: `stdlib/net.hy`. Low-level IPv4/TCP sockets over raw syscalls. Deliberately
minimal: IPv4 + TCP only, no DNS/`getaddrinfo` — callers pass a raw dotted-quad IP
string.

| Function | Signature | Description |
|---|---|---|
| `tcp_socket` | `int tcp_socket()` | Create a TCP/IPv4 socket. Returns a file descriptor, or `-1`. |
| `connect` | `int connect(int fd, str ip, int port)` | Connect to `ip:port`. Returns `0` on success. |
| `bind` | `int bind(int fd, str ip, int port)` | Bind to `ip:port` (`"0.0.0.0"` for all interfaces). Returns `0` on success. |
| `listen` | `int listen(int fd, int backlog)` | Mark a bound socket as accepting connections. |
| `accept` | `int accept(int fd)` | Accept one incoming connection, returning a new fd (or `-1`). |
| `send` | `int send(int fd, str buf, int len)` | Send `len` bytes from `buf` on a connected socket. |
| `recv` | `int recv(int fd, str buf, int len)` | Receive up to `len` bytes into `buf`. |
| `close` | `void close(int fd)` | Close a socket. |
| `shutdown` | `int shutdown(int fd, int how)` | Shut down part or all of a full-duplex connection. `how`: `0`=read, `1`=write, `2`=both. |

```hylian
include { net, io, }

int main() {
    int fd = tcp_socket();
    if (connect(fd, "93.184.216.34", 80) != 0) {
        eprintln("connect failed");
        return 1;
    }
    close(fd);
    return 0;
}
```

Internally, `net.hy` builds a 16-byte `struct sockaddr_in` by hand (no real struct type
for it — see `_build_sockaddr_in` in the source) and converts port numbers to network
byte order itself (`_htons`), since x86-64 is little-endian and network byte order is
big-endian.
