# std.networking.udp

UDP (User Datagram Protocol) networking for Hylian programs. Supports connectionless and connected usage patterns, timeouts, broadcasting, and IPv4 multicast.

```hylian
include {
    std.networking.udp,
}
```

UDP is a **connectionless, unreliable** transport. Datagrams may be lost, reordered, or duplicated in transit. There is no handshake, no guaranteed delivery, and no flow control. In exchange, UDP offers minimal overhead and is well suited to latency-sensitive applications: games, DNS queries, media streaming, and service discovery.

> **Note:** The compiler rewrites `udp_` calls to `hylian_net_udp_` symbols automatically. You write `udp_socket(...)` in source; the linker resolves `hylian_net_udp_socket`.

---

## Usage Patterns

### Connectionless (send to / receive from any address)

```hylian
int fd = udp_socket();
udp_bind(fd, 9000);
// ... udp_send_to / udp_recv_from ...
udp_close(fd);
```

### Connected (fixed remote address)

```hylian
int fd = udp_socket();
udp_connect(fd, "10.0.0.1", 9000);
// ... udp_send / udp_recv ...
udp_close(fd);
```

---

## Functions

### `udp_socket() -> int`

Create a new UDP socket.

The socket is dual-stack (accepts both IPv4 and IPv6) where the platform supports it, falling back to IPv4-only otherwise. The returned fd must be passed to all subsequent `udp_*` functions and closed with `udp_close` when no longer needed.

**Returns:** a socket fd (`>= 0`) on success, or `-1` on error.

```hylian
include {
    std.networking.udp,
    std.io,
}

Error? main() {
    int fd = udp_socket();
    if (fd < 0) {
        println("Failed to create UDP socket");
        return nil;
    }
    println("Socket fd = {{fd}}");
    udp_close(fd);
    return nil;
}
```

---

### `udp_bind(fd, port) -> int`

Bind a UDP socket to a local port on all interfaces.

Must be called before `udp_recv_from` if you want to receive inbound datagrams. Senders do not need to bind — the OS assigns an ephemeral port automatically when `udp_send_to` is first called.

| Parameter | Type  | Description                         |
|-----------|-------|-------------------------------------|
| `fd`      | `int` | Socket fd returned by `udp_socket`  |
| `port`    | `int` | Local port number (1–65535)         |

**Returns:** `0` on success, or `-1` on error (port already in use, permission denied, etc.).

```hylian
int rc = udp_bind(fd, 9000);
if (rc < 0) {
    println("Could not bind port 9000");
}
```

---

### `udp_send_to(fd, host, host_len, port, buf, len) -> int`

Send a datagram to a specific remote address.

`host` is resolved via DNS and does **not** need to be null-terminated — pass the string and its byte length separately. Each call may specify a different destination, making this suitable for protocols that fan out to multiple peers.

| Parameter   | Type  | Description                                       |
|-------------|-------|---------------------------------------------------|
| `fd`        | `int` | Socket fd                                         |
| `host`      | `str` | Destination host — IP address or hostname         |
| `host_len`  | `int` | Byte length of `host`                             |
| `port`      | `int` | Destination port number                           |
| `buf`       | `str` | Datagram payload to send                          |
| `len`       | `int` | Number of bytes to send from `buf`                |

**Returns:** the number of bytes sent on success, or `-1` on error.

> **Caution:** A successful return only means the datagram was handed to the OS. It does not guarantee delivery.

```hylian
str msg = "hello";
int sent = udp_send_to(fd, "192.168.1.10", 12, 9000, msg, 5);
if (sent < 0) {
    println("Send failed");
}
```

---

### `udp_recv_from(fd, buf, buf_len, src_addr_buf, src_addr_buflen, src_port_out) -> int`

Receive one datagram from any sender.

Blocks until a datagram arrives (unless the socket is non-blocking). Writes the sender's IP address as a null-terminated string into `src_addr_buf` and the sender's port number into `src_port_out`.

If the arriving datagram is larger than `buf_len`, the excess bytes are **silently discarded** — there is no error.

| Parameter         | Type  | Description                                                           |
|-------------------|-------|-----------------------------------------------------------------------|
| `fd`              | `int` | Socket fd (must have been bound with `udp_bind`)                      |
| `buf`             | `str` | Buffer to receive the datagram payload into                           |
| `buf_len`         | `int` | Capacity of `buf` in bytes                                            |
| `src_addr_buf`    | `str` | Buffer to receive the sender's IP address string into                 |
| `src_addr_buflen` | `int` | Capacity of `src_addr_buf` — must be **at least 46 bytes** for IPv6  |
| `src_port_out`    | `int` | Written with the sender's port number                                 |

**Returns:** the number of bytes received on success, or `-1` on error.

```hylian
array<int, 1024> buf;
array<int, 46>   sender_ip;
int              sender_port;

int n = udp_recv_from(fd, buf, 1024, sender_ip, 46, sender_port);
if (n < 0) {
    println("Recv error");
} else {
    println("Got {{n}} bytes from {{sender_ip}}:{{sender_port}}");
}
```

---

### `udp_connect(fd, host, host_len, port) -> int`

Associate a UDP socket with a fixed remote address.

After connecting, `udp_send` and `udp_recv` can be used without specifying a destination on each call, and only datagrams from the connected peer are delivered to `udp_recv`. The socket still uses UDP — datagrams may still be lost.

`host` does **not** need to be null-terminated.

| Parameter  | Type  | Description                                 |
|------------|-------|---------------------------------------------|
| `fd`       | `int` | Socket fd returned by `udp_socket`          |
| `host`     | `str` | Remote host — IP address or hostname        |
| `host_len` | `int` | Byte length of `host`                       |
| `port`     | `int` | Remote port number                          |

**Returns:** `0` on success, or `-1` on error.

```hylian
int rc = udp_connect(fd, "10.0.0.1", 8, 9000);
if (rc < 0) {
    println("udp_connect failed");
}
```

---

### `udp_send(fd, buf, len) -> int`

Send a datagram over a socket that was previously associated with a remote address via `udp_connect`.

| Parameter | Type  | Description                                    |
|-----------|-------|------------------------------------------------|
| `fd`      | `int` | Connected socket fd                            |
| `buf`     | `str` | Datagram payload                               |
| `len`     | `int` | Number of bytes to send from `buf`             |

**Returns:** the number of bytes sent on success, or `-1` on error.

```hylian
int sent = udp_send(fd, "ping", 4);
if (sent < 0) {
    println("Send failed");
}
```

---

### `udp_recv(fd, buf, buf_len) -> int`

Receive a datagram over a socket that was previously associated with a remote address via `udp_connect`.

Only datagrams originating from the connected remote address are delivered. Blocks until a datagram arrives unless the socket is non-blocking.

| Parameter | Type  | Description                             |
|-----------|-------|-----------------------------------------|
| `fd`      | `int` | Connected socket fd                     |
| `buf`     | `str` | Buffer to receive the datagram into     |
| `buf_len` | `int` | Capacity of `buf` in bytes              |

**Returns:** the number of bytes received on success, or `-1` on error.

```hylian
array<int, 512> buf;
int n = udp_recv(fd, buf, 512);
if (n >= 0) {
    println("Received {{n}} bytes");
}
```

---

### `udp_close(fd) -> int`

Close a UDP socket and release its file descriptor.

The fd must not be used after this call. Failing to close sockets will leak file descriptors.

| Parameter | Type  | Description          |
|-----------|-------|----------------------|
| `fd`      | `int` | Socket fd to close   |

**Returns:** `0` on success, or `-1` on error.

```hylian
udp_close(fd);
```

---

### Socket Options

#### `udp_set_nonblocking(fd, enable) -> int`

Switch a socket between blocking and non-blocking mode.

In non-blocking mode, `udp_send`, `udp_recv`, `udp_send_to`, and `udp_recv_from` return `-1` immediately instead of blocking when the operation cannot complete.

On POSIX this uses `fcntl(F_SETFL, O_NONBLOCK)`; on Windows it uses `ioctlsocket(FIONBIO)`.

| Parameter | Type  | Description                                  |
|-----------|-------|----------------------------------------------|
| `fd`      | `int` | Socket fd                                    |
| `enable`  | `int` | `1` to enable non-blocking, `0` to disable   |

**Returns:** `0` on success, or `-1` on error.

```hylian
udp_set_nonblocking(fd, 1); // non-blocking
udp_set_nonblocking(fd, 0); // back to blocking
```

---

#### `udp_set_timeout(fd, which, ms) -> int`

Set a send or receive timeout on a socket.

If the blocked operation does not complete within the timeout it returns `-1`. Pass `ms = 0` to disable the timeout and restore the default blocking behaviour.

| Parameter | Type  | Description                                                          |
|-----------|-------|----------------------------------------------------------------------|
| `fd`      | `int` | Socket fd                                                            |
| `which`   | `int` | `0` = receive timeout (`SO_RCVTIMEO`), `1` = send timeout (`SO_SNDTIMEO`) |
| `ms`      | `int` | Timeout in milliseconds; `0` disables the timeout                   |

**Returns:** `0` on success, or `-1` on error.

```hylian
udp_set_timeout(fd, 0, 2000); // 2-second receive timeout
udp_set_timeout(fd, 1, 2000); // 2-second send timeout
udp_set_timeout(fd, 0, 0);    // disable receive timeout
```

---

#### `udp_set_broadcast(fd, enable) -> int`

Enable or disable sending datagrams to broadcast addresses (`SO_BROADCAST`).

When enabled, you may send to the local broadcast address (e.g. `255.255.255.255`) and all hosts on the local network segment will receive the datagram — subject to UDP's usual unreliable delivery.

| Parameter | Type  | Description                               |
|-----------|-------|-------------------------------------------|
| `fd`      | `int` | Socket fd                                 |
| `enable`  | `int` | `1` to enable broadcasting, `0` to disable |

**Returns:** `0` on success, or `-1` on error.

```hylian
udp_set_broadcast(fd, 1);
udp_send_to(fd, "255.255.255.255", 15, 9999, "announce", 8);
```

---

#### `udp_join_multicast(fd, group_addr, group_addr_len) -> int`

Join an IPv4 multicast group.

After joining, the socket will receive datagrams sent to the given multicast address. `group_addr` must be a valid IPv4 multicast address in dotted-decimal notation (range `224.0.0.0` – `239.255.255.255`). Multiple groups may be joined on the same socket.

`group_addr` does **not** need to be null-terminated.

| Parameter        | Type  | Description                                             |
|------------------|-------|---------------------------------------------------------|
| `fd`             | `int` | Socket fd (should be bound to the multicast port first) |
| `group_addr`     | `str` | Multicast group IPv4 address string                     |
| `group_addr_len` | `int` | Byte length of `group_addr`                             |

**Returns:** `0` on success, or `-1` on error (invalid address, not a multicast range, etc.).

```hylian
int rc = udp_join_multicast(fd, "239.0.0.1", 9);
if (rc < 0) {
    println("Failed to join multicast group");
}
```

---

## Complete Examples

### Sender

Send ten numbered datagrams to a remote listener and exit.

```hylian
include {
    std.networking.udp,
    std.io,
}

Error? main() {
    int fd = udp_socket();
    if (fd < 0) {
        println("Failed to create socket");
        return nil;
    }

    // Set a 3-second send timeout so we don't block indefinitely.
    udp_set_timeout(fd, 1, 3000);

    int i = 0;
    while (i < 10) {
        str msg = "datagram {{i}}";
        int sent = udp_send_to(fd, "127.0.0.1", 9, 9000, msg, 13);
        if (sent < 0) {
            println("Send failed on datagram {{i}}");
        } else {
            println("Sent datagram {{i}} ({{sent}} bytes)");
        }
        i = i + 1;
    }

    udp_close(fd);
    println("Sender done.");
    return nil;
}
```

---

### Receiver

Bind to a port, receive datagrams, print the sender address and payload, and run until interrupted.

```hylian
include {
    std.networking.udp,
    std.io,
}

Error? main() {
    int fd = udp_socket();
    if (fd < 0) {
        println("Failed to create socket");
        return nil;
    }

    int rc = udp_bind(fd, 9000);
    if (rc < 0) {
        println("Could not bind port 9000");
        udp_close(fd);
        return nil;
    }
    println("Listening for UDP datagrams on :9000");

    array<int, 1024> buf;
    array<int, 46>   sender_ip;
    int              sender_port;

    while (true) {
        int n = udp_recv_from(fd, buf, 1024, sender_ip, 46, sender_port);
        if (n < 0) {
            println("Recv error — continuing");
            continue;
        }
        println("[{{sender_ip}}:{{sender_port}}] {{n}} bytes: {{buf}}");
    }

    udp_close(fd);
    return nil;
}
```

---

## Caveats

- **Datagrams may be lost.** There is no retransmission. If reliability matters, implement acknowledgements at the application level or use TCP instead.
- **Datagrams may arrive out of order.** Include sequence numbers in your payload if ordering is important.
- **Datagrams may be duplicated.** The receiver should tolerate receiving the same payload more than once.
- **No connection state.** `udp_connect` is a local kernel convenience — it does not send any packets to the peer and does not establish a session.
- **Maximum payload size.** A single UDP datagram payload should generally stay under 1472 bytes to avoid IP fragmentation on Ethernet networks. Larger payloads are allowed but fragmentation increases the chance of packet loss.