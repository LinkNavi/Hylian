# std.networking.tcp

Low-level TCP networking for Hylian programs. Supports both client and server roles, socket option helpers, and optional non-blocking I/O.

```hylian
include {
    std.networking.tcp,
}
```

The TCP API is built around integer **file descriptors (fds)**. `tcp_connect` and `tcp_accept` each return an fd that is then passed to every subsequent call. Always close fds with `tcp_close` when they are no longer needed.

> **Note:** The compiler rewrites `tcp_` calls to `hylian_net_tcp_` symbols automatically. You write `tcp_connect(...)` in source; the linker resolves `hylian_net_tcp_connect`.

---

## Usage Patterns

### Client

```hylian
int fd = tcp_connect("example.com", 80);
// ... tcp_send / tcp_recv ...
tcp_close(fd);
```

### Server

```hylian
int listener = tcp_listen(8080, 128);
int client   = tcp_accept(listener);
// ... tcp_send / tcp_recv on client ...
tcp_close(client);
tcp_close(listener);
```

---

## Functions

### Client Functions

#### `tcp_connect(host, port) -> int`

Connect to a remote host over TCP.

`host` may be an IPv4 address, IPv6 address, or a hostname resolved via DNS. Both IPv4 and IPv6 are tried automatically.

| Parameter | Type  | Description                                                    |
|-----------|-------|----------------------------------------------------------------|
| `host`    | `str` | Remote host — IP address or hostname                           |
| `port`    | `int` | Remote port number (1–65535)                                   |

**Returns:** a connected socket fd (`>= 0`) on success, or `-1` on error (host unreachable, connection refused, etc.).

```hylian
include {
    std.networking.tcp,
    std.io,
}

Error? main() {
    int fd = tcp_connect("example.com", 80);
    if (fd < 0) {
        println("Connection failed");
        return nil;
    }
    println("Connected, fd = {{fd}}");
    tcp_close(fd);
    return nil;
}
```

---

#### `tcp_send(fd, buf, len) -> int`

Send bytes over a connected socket.

The call may send fewer bytes than requested — check the return value and loop if you need guaranteed full delivery.

| Parameter | Type  | Description                                         |
|-----------|-------|-----------------------------------------------------|
| `fd`      | `int` | Socket fd returned by `tcp_connect` or `tcp_accept` |
| `buf`     | `str` | Data to send                                        |
| `len`     | `int` | Number of bytes to send from `buf`                  |

**Returns:** the number of bytes actually sent (`>= 0`), or `-1` on error.

```hylian
str msg = "ping";
int sent = tcp_send(fd, msg, 4);
if (sent < 0) {
    println("Send failed");
}
```

---

#### `tcp_recv(fd, buf, buf_len) -> int`

Receive bytes from a connected socket into a buffer.

Blocks until at least one byte is available (unless the socket is non-blocking). A return value of `0` means the remote end closed the connection cleanly.

| Parameter | Type  | Description                                         |
|-----------|-------|-----------------------------------------------------|
| `fd`      | `int` | Socket fd                                           |
| `buf`     | `str` | Buffer to receive data into                         |
| `buf_len` | `int` | Capacity of `buf` in bytes                          |

**Returns:** the number of bytes received (`> 0`), `0` if the connection was closed by the peer, or `-1` on error.

```hylian
array<int, 1024> buf;
int n = tcp_recv(fd, buf, 1024);
if (n < 0) {
    println("Recv error");
} else if (n == 0) {
    println("Connection closed by peer");
} else {
    println("Received {{n}} bytes");
}
```

---

#### `tcp_close(fd) -> int`

Close a socket fd and release its OS resources.

Should be called on every fd returned by `tcp_connect`, `tcp_accept`, and `tcp_listen` once it is no longer needed. Failing to close fds will leak file descriptors.

| Parameter | Type  | Description          |
|-----------|-------|----------------------|
| `fd`      | `int` | Socket fd to close   |

**Returns:** `0` on success, or `-1` on error.

```hylian
tcp_close(fd);
```

---

### Server Functions

#### `tcp_listen(port, backlog) -> int`

Bind a TCP server socket to all interfaces on the given port and begin listening for incoming connections.

`SO_REUSEADDR` is set automatically so the port can be rebound immediately after the server exits. When IPv6 is available the socket is dual-stack (accepts both IPv4 and IPv6 clients); it falls back to IPv4-only otherwise.

| Parameter | Type  | Description                                                          |
|-----------|-------|----------------------------------------------------------------------|
| `port`    | `int` | Local port to listen on (1–65535)                                    |
| `backlog` | `int` | Maximum number of pending connections waiting to be `tcp_accept`ed   |

**Returns:** a listener fd (`>= 0`) on success, or `-1` on error (port in use, permission denied, etc.).

```hylian
int listener = tcp_listen(8080, 128);
if (listener < 0) {
    println("Could not bind port 8080");
    return nil;
}
println("Listening on :8080");
```

---

#### `tcp_accept(listener_fd) -> int`

Accept the next incoming connection on a listener socket.

Blocks until a client connects (unless the listener is set to non-blocking mode). Each successful call returns a new, independent client fd that must be closed separately from the listener.

| Parameter     | Type  | Description                                        |
|---------------|-------|----------------------------------------------------|
| `listener_fd` | `int` | Listener fd returned by `tcp_listen`               |

**Returns:** a new client socket fd (`>= 0`) on success, or `-1` on error.

```hylian
int client = tcp_accept(listener);
if (client < 0) {
    println("Accept failed");
    return nil;
}
println("Client connected, fd = {{client}}");
```

---

### Socket Options

#### `tcp_set_nonblocking(fd, enable) -> int`

Switch a socket between blocking and non-blocking mode.

In non-blocking mode, `tcp_send`, `tcp_recv`, and `tcp_accept` return `-1` immediately instead of blocking when the operation cannot complete right away.

On POSIX this uses `fcntl(F_SETFL, O_NONBLOCK)`; on Windows it uses `ioctlsocket(FIONBIO)`.

| Parameter | Type  | Description                            |
|-----------|-------|----------------------------------------|
| `fd`      | `int` | Socket fd                              |
| `enable`  | `int` | `1` to enable non-blocking, `0` to disable |

**Returns:** `0` on success, or `-1` on error.

```hylian
tcp_set_nonblocking(fd, 1); // non-blocking
tcp_set_nonblocking(fd, 0); // back to blocking
```

---

#### `tcp_set_reuseaddr(fd, enable) -> int`

Set or clear `SO_REUSEADDR` on a socket.

When set, the port used by this socket can be rebound immediately after it is closed — useful for servers that restart frequently. `tcp_listen` enables this automatically; this function is provided for cases where you build a socket manually.

| Parameter | Type  | Description                                      |
|-----------|-------|--------------------------------------------------|
| `fd`      | `int` | Socket fd                                        |
| `enable`  | `int` | `1` to enable `SO_REUSEADDR`, `0` to disable     |

**Returns:** `0` on success, or `-1` on error.

```hylian
tcp_set_reuseaddr(fd, 1);
```

---

#### `tcp_set_timeout(fd, which, ms) -> int`

Set a send or receive timeout on a socket.

If the blocked operation does not complete within the timeout it returns `-1`. Pass `ms = 0` to disable the timeout and restore the default blocking behaviour.

| Parameter | Type  | Description                                                    |
|-----------|-------|----------------------------------------------------------------|
| `fd`      | `int` | Socket fd                                                      |
| `which`   | `int` | `0` = receive timeout (`SO_RCVTIMEO`), `1` = send timeout (`SO_SNDTIMEO`) |
| `ms`      | `int` | Timeout in milliseconds; `0` disables the timeout             |

**Returns:** `0` on success, or `-1` on error.

```hylian
tcp_set_timeout(fd, 0, 5000); // 5-second receive timeout
tcp_set_timeout(fd, 1, 5000); // 5-second send timeout
tcp_set_timeout(fd, 0, 0);    // disable receive timeout
```

---

## Complete Examples

### HTTP GET Client

Connect to a server, send a raw HTTP/1.1 GET request, receive the response, and close the connection.

```hylian
include {
    std.networking.tcp,
    std.io,
}

Error? main() {
    // 1. Connect to the remote host on port 80.
    int fd = tcp_connect("example.com", 80);
    if (fd < 0) {
        println("Could not connect to example.com:80");
        return nil;
    }

    // 2. Set a 10-second receive timeout so we don't block forever.
    tcp_set_timeout(fd, 0, 10000);

    // 3. Send a minimal HTTP/1.1 GET request.
    str req = "GET / HTTP/1.1\r\nHost: example.com\r\nConnection: close\r\n\r\n";
    int sent = tcp_send(fd, req, 58);
    if (sent < 0) {
        println("Send failed");
        tcp_close(fd);
        return nil;
    }

    // 4. Receive the response in a loop until the connection closes.
    array<int, 4096> buf;
    int total = 0;

    while (true) {
        int n = tcp_recv(fd, buf, 4096);
        if (n < 0) {
            println("Recv error after {{total}} bytes");
            break;
        }
        if (n == 0) {
            break; // server closed the connection cleanly
        }
        total = total + n;
    }

    println("Received {{total}} bytes total");

    // 5. Always close the socket when done.
    tcp_close(fd);
    return nil;
}
```

---

### Echo Server

Listen for connections, echo every received message back to the sender, and close when the client disconnects.

```hylian
include {
    std.networking.tcp,
    std.io,
}

Error? main() {
    // 1. Bind and listen. SO_REUSEADDR is set automatically by tcp_listen.
    int listener = tcp_listen(9000, 128);
    if (listener < 0) {
        println("Could not bind port 9000");
        return nil;
    }
    println("Echo server listening on :9000");

    // 2. Accept clients in a loop.
    while (true) {
        int client = tcp_accept(listener);
        if (client < 0) {
            println("Accept failed — retrying");
            continue;
        }
        println("Client connected");

        // Give the client a 30-second inactivity timeout.
        tcp_set_timeout(client, 0, 30000);

        // 3. Echo loop: read a chunk, send it straight back.
        array<int, 1024> buf;
        while (true) {
            int n = tcp_recv(client, buf, 1024);
            if (n <= 0) {
                break; // 0 = clean close, -1 = error / timeout
            }

            int sent = tcp_send(client, buf, n);
            if (sent < 0) {
                println("Send error — dropping client");
                break;
            }
        }

        // 4. Close the client fd. The listener stays open for the next one.
        tcp_close(client);
        println("Client disconnected");
    }

    tcp_close(listener);
    return nil;
}
```
