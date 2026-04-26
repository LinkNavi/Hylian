# std.networking.https

HTTPS (HTTP over TLS) networking for Hylian programs. Backed by OpenSSL, this module provides both a high-level one-shot request API and a low-level handle-based streaming API, plus helpers for parsing raw HTTP responses.

```hylian
include {
    std.networking.https,
}
```

> **Linking:** This module requires OpenSSL. Pass `-lssl -lcrypto` when linking your program:
> ```sh
> gcc myprogram.o runtime/std/networking/https.o -lssl -lcrypto -o myprogram -no-pie
> ```

> **Note:** The compiler rewrites `https_` calls to `hylian_net_https_` symbols automatically. You write `https_get(...)` in source; the linker resolves `hylian_net_https_get`.

---

## Usage Patterns

For most programs, the high-level functions are all you need:

```hylian
// One-shot GET
array<int, 65536> buf;
int n = https_get("api.example.com", 15, "/v1/status", 10, buf, 65536);

// One-shot POST
str payload = "{\"key\": \"value\"}";
int n = https_post("api.example.com", 15, "/v1/data", 8,
                   "application/json", 16, payload, 16, buf, 65536);
```

For streaming responses or keep-alive control, use the low-level handle API:

```hylian
int h = https_connect("example.com", 11, 443, 1);
https_send(h, request, request_len);
// ... https_recv in a loop ...
https_close(h); // always close to free the slot
```

---

## Functions

### High-Level API

These functions manage the full connection lifecycle internally — connect, send, receive, and close — all in one call. They are the right choice for the vast majority of programs.

---

#### `https_get(host, host_len, path, path_len, out_buf, out_buflen) -> int`

Perform a complete HTTPS GET request and collect the full response into a buffer.

Connects to `https://<host><path>` on port 443, sends an HTTP/1.1 GET request, reads the entire response (status line + headers + body), closes the connection, and returns. Certificate verification is always enabled.

| Parameter    | Type  | Description                                                 |
|--------------|-------|-------------------------------------------------------------|
| `host`       | `str` | Remote hostname (not null-terminated)                       |
| `host_len`   | `int` | Byte length of `host`                                       |
| `path`       | `str` | Request path, e.g. `"/v1/users"` (not null-terminated)      |
| `path_len`   | `int` | Byte length of `path`                                       |
| `out_buf`    | `str` | Buffer to write the raw HTTP response into                  |
| `out_buflen` | `int` | Capacity of `out_buf` in bytes                              |

**Returns:** the total number of bytes written into `out_buf` (headers + body), or `-1` on error (DNS failure, TLS handshake failure, network error, etc.).

> **Tip:** The response written into `out_buf` includes the full HTTP status line and headers. Use `https_status` and `https_body` to extract the parts you need.

```hylian
include {
    std.networking.https,
    std.io,
}

Error? main() {
    array<int, 65536> buf;

    int n = https_get("api.example.com", 15, "/v1/status", 10, buf, 65536);
    if (n < 0) {
        println("GET failed");
        return nil;
    }

    println("Response ({{n}} bytes):");
    println("{{buf}}");
    return nil;
}
```

---

#### `https_post(host, host_len, path, path_len, content_type, ct_len, body, body_len, out_buf, out_buflen) -> int`

Perform a complete HTTPS POST request and collect the full response into a buffer.

Connects to `https://<host><path>` on port 443, sends an HTTP/1.1 POST request with the given `Content-Type` header and request body, reads the entire response, and closes the connection. Certificate verification is always enabled.

| Parameter      | Type  | Description                                                        |
|----------------|-------|--------------------------------------------------------------------|
| `host`         | `str` | Remote hostname (not null-terminated)                              |
| `host_len`     | `int` | Byte length of `host`                                              |
| `path`         | `str` | Request path (not null-terminated)                                 |
| `path_len`     | `int` | Byte length of `path`                                              |
| `content_type` | `str` | Value of the `Content-Type` header, e.g. `"application/json"`     |
| `ct_len`       | `int` | Byte length of `content_type`                                      |
| `body`         | `str` | Raw request body bytes                                             |
| `body_len`     | `int` | Number of bytes in `body`                                          |
| `out_buf`      | `str` | Buffer to write the raw HTTP response into                         |
| `out_buflen`   | `int` | Capacity of `out_buf` in bytes                                     |

**Returns:** the total number of bytes written into `out_buf`, or `-1` on error.

```hylian
include {
    std.networking.https,
    std.io,
}

Error? main() {
    array<int, 65536> buf;
    str payload = "{\"name\": \"alice\", \"score\": 42}";

    int n = https_post(
        "api.example.com", 15,
        "/v1/scores",      10,
        "application/json", 16,
        payload,           29,
        buf,               65536
    );

    if (n < 0) {
        println("POST failed");
        return nil;
    }

    int status = https_status(buf, n);
    println("Status: {{status}}");
    return nil;
}
```

---

### Low-Level Handle-Based API

The low-level API gives you direct control over the TLS connection lifecycle. Use it when you need to stream a large response chunk by chunk, reuse a connection for multiple requests, or inspect intermediate state.

Each open connection occupies a slot in an internal table. **The table holds at most 64 simultaneous connections.** Always call `https_close` when you are finished — even on error paths — to free the slot.

---

#### `https_connect(host, host_len, port, verify_peer) -> int`

Open a TLS connection to a remote host and return a connection handle.

Resolves `host` via DNS, establishes a TCP connection to `port`, performs a TLS handshake (including SNI), and optionally verifies the server certificate against the system CA bundle.

| Parameter     | Type  | Description                                                                      |
|---------------|-------|----------------------------------------------------------------------------------|
| `host`        | `str` | Remote hostname (not null-terminated)                                            |
| `host_len`    | `int` | Byte length of `host`                                                            |
| `port`        | `int` | Remote port number (typically `443`)                                             |
| `verify_peer` | `int` | `1` to verify the server certificate (recommended); `0` to skip verification     |

**Returns:** a connection handle (`0`–`63`) on success, or `-1` if the connection failed or no free slot is available.

> **Certificate verification:** `verify_peer = 1` validates the server's certificate against the system CA bundle. Always use `1` in production. `verify_peer = 0` disables verification entirely — use only during local development or testing against self-signed certificates.

```hylian
int h = https_connect("example.com", 11, 443, 1);
if (h < 0) {
    println("TLS connect failed");
    return nil;
}
```

---

#### `https_send(handle, buf, len) -> int`

Send raw bytes over an open TLS connection.

You are responsible for formatting a valid HTTP request. The call may send fewer bytes than requested — check the return value and loop if you need full delivery.

| Parameter | Type  | Description                                          |
|-----------|-------|------------------------------------------------------|
| `handle`  | `int` | Connection handle returned by `https_connect`        |
| `buf`     | `str` | Data to send                                         |
| `len`     | `int` | Number of bytes to send from `buf`                   |

**Returns:** the number of bytes sent (`>= 0`), or `-1` on error.

```hylian
str req = "GET / HTTP/1.1\r\nHost: example.com\r\nConnection: close\r\n\r\n";
int sent = https_send(h, req, 58);
if (sent < 0) {
    println("Send failed");
    https_close(h);
    return nil;
}
```

---

#### `https_recv(handle, buf, buf_len) -> int`

Receive bytes from an open TLS connection into a buffer.

Blocks until data is available (or the connection is closed). Call in a loop until `0` is returned to collect the full response.

| Parameter | Type  | Description                                              |
|-----------|-------|----------------------------------------------------------|
| `handle`  | `int` | Connection handle                                        |
| `buf`     | `str` | Buffer to receive data into                              |
| `buf_len` | `int` | Capacity of `buf` in bytes                               |

**Returns:** the number of bytes received (`> 0`), `0` if the server closed the connection cleanly, or `-1` on error.

```hylian
array<int, 4096> chunk;
int total = 0;

while (true) {
    int n = https_recv(h, chunk, 4096);
    if (n < 0) {
        println("Recv error");
        break;
    }
    if (n == 0) {
        break; // connection closed cleanly — response complete
    }
    total = total + n;
}
println("Received {{total}} bytes total");
```

---

#### `https_close(handle) -> int`

Shut down a TLS connection and release all associated resources.

Performs a clean TLS shutdown, closes the underlying TCP socket, and frees the internal connection slot so it can be reused. After this call the handle value is invalid.

**Always call `https_close`** — even if `https_send` or `https_recv` returned an error — to avoid leaking connection slots.

| Parameter | Type  | Description                        |
|-----------|----- -|------------------------------------|
| `handle`  | `int` | Connection handle to close         |

**Returns:** `0` on success, or `-1` if the handle was invalid.

```hylian
https_close(h);
```

---

### Response Parsing Helpers

These functions parse raw HTTP response buffers returned by the high-level or low-level API.

---

#### `https_body(response, response_len, out_buf, out_buflen) -> int`

Extract the HTTP response body from a raw response buffer.

Scans the buffer for the blank line (`\r\n\r\n`) that separates HTTP headers from the body, then copies everything after it into `out_buf`.

| Parameter      | Type  | Description                                                   |
|----------------|-------|---------------------------------------------------------------|
| `response`     | `str` | Raw HTTP response buffer (headers + body)                     |
| `response_len` | `int` | Total number of valid bytes in `response`                     |
| `out_buf`      | `str` | Buffer to copy the body into                                  |
| `out_buflen`   | `int` | Capacity of `out_buf` in bytes                                |

**Returns:** the number of bytes copied into `out_buf`, `0` if the response has no body, or `-1` if no header/body separator was found (incomplete response).

```hylian
array<int, 65536> response;
array<int, 65536> body;

int n    = https_get("example.com", 11, "/", 1, response, 65536);
int blen = https_body(response, n, body, 65536);

if (blen < 0) {
    println("Could not find response body");
} else {
    println("Body ({{blen}} bytes): {{body}}");
}
```

---

#### `https_status(response, response_len) -> int`

Parse the HTTP status code from a raw response buffer.

Reads the first line of the response, which must be of the form `HTTP/1.x NNN Reason Phrase`, and extracts the three-digit status code.

| Parameter      | Type  | Description                               |
|----------------|-------|-------------------------------------------|
| `response`     | `str` | Raw HTTP response buffer                  |
| `response_len` | `int` | Total number of valid bytes in `response` |

**Returns:** the HTTP status code (e.g. `200`, `404`, `500`), or `-1` if the response could not be parsed.

```hylian
array<int, 65536> response;

int n      = https_get("example.com", 11, "/", 1, response, 65536);
int status = https_status(response, n);

if (status != 200) {
    println("Unexpected status: {{status}}");
}
```

---

## Complete Examples

### Example 1 — Simple GET and Print Body

Fetch a resource over HTTPS, parse out the body, and print it.

```hylian
include {
    std.networking.https,
    std.io,
}

Error? main() {
    array<int, 65536> response;
    array<int, 65536> body;

    // Perform the GET request. https_get always uses port 443 with
    // certificate verification enabled.
    int n = https_get("httpbin.org", 11, "/get", 4, response, 65536);
    if (n < 0) {
        println("GET request failed");
        return nil;
    }

    // Check the HTTP status code.
    int status = https_status(response, n);
    if (status != 200) {
        println("Server returned status {{status}}");
        return nil;
    }

    // Extract just the body (everything after the headers).
    int blen = https_body(response, n, body, 65536);
    if (blen < 0) {
        println("Could not parse response body");
        return nil;
    }

    println("Response body ({{blen}} bytes):");
    println("{{body}}");
    return nil;
}
```

---

### Example 2 — POST JSON and Check Status

Send a JSON payload to an API endpoint and verify the response status.

```hylian
include {
    std.networking.https,
    std.io,
}

Error? main() {
    array<int, 65536> response;
    array<int, 65536> body;

    str host    = "httpbin.org";
    str path    = "/post";
    str ctype   = "application/json";
    str payload = "{\"user\": \"alice\", \"action\": \"login\"}";

    int n = https_post(
        host,    11,
        path,    5,
        ctype,   16,
        payload, 35,
        response, 65536
    );

    if (n < 0) {
        println("POST request failed");
        return nil;
    }

    // A 200 status means the server accepted the request.
    int status = https_status(response, n);
    if (status == 200) {
        println("POST accepted");

        int blen = https_body(response, n, body, 65536);
        if (blen >= 0) {
            println("Response body: {{body}}");
        }
    } else {
        println("Server returned status {{status}}");
    }

    return nil;
}
```

---

### Example 3 — Low-Level Streaming with connect / send / recv / close

Use the handle-based API to build and send a raw HTTP request, then stream the response in chunks. Useful for large responses or when you need fine-grained control.

```hylian
include {
    std.networking.https,
    std.io,
}

Error? main() {
    // 1. Open a TLS connection. verify_peer=1 validates the certificate.
    int h = https_connect("httpbin.org", 11, 443, 1);
    if (h < 0) {
        println("TLS handshake failed");
        return nil;
    }

    // 2. Build and send a raw HTTP/1.1 request.
    str req =
        "GET /stream/5 HTTP/1.1\r\n"
        "Host: httpbin.org\r\n"
        "Accept: application/json\r\n"
        "Connection: close\r\n"
        "\r\n";

    int sent = https_send(h, req, 84);
    if (sent < 0) {
        println("Failed to send request");
        https_close(h);
        return nil;
    }

    // 3. Receive the response in 4 KiB chunks until the server closes
    //    the connection (n == 0) or an error occurs (n < 0).
    array<int, 4096> chunk;
    int total   = 0;
    int chunks  = 0;
    bool header_done = false;

    while (true) {
        int n = https_recv(h, chunk, 4096);
        if (n < 0) {
            println("Recv error after {{total}} bytes");
            break;
        }
        if (n == 0) {
            break; // clean TLS close — response complete
        }

        total  = total  + n;
        chunks = chunks + 1;
        println("[chunk {{chunks}}] {{n}} bytes");
    }

    println("Stream complete: {{total}} bytes in {{chunks}} chunk(s)");

    // 4. Always close the handle to free the internal connection slot.
    https_close(h);
    return nil;
}
```
