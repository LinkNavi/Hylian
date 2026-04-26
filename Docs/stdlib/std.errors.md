## std.errors

Error handling primitives for recoverable failures and hard crashes.

`Err` and `panic` are **built-in** — they do not require an include. The `Error` type is always available. The `std.errors` module only needs to be included if you want to use `Error` as an explicit type annotation in more advanced patterns.

```hylian
// Optional — only needed for explicit Error type usage
include {
    std.errors,
}
```

---

### Overview

The `Error` type represents a recoverable failure. Functions that can fail return `Error?` (a nullable `Error` pointer). A return value of `nil` means success; a non-nil value means failure and carries a message.

**Memory layout — 16 bytes:**

| Offset | Type | Field | Description |
|---|---|---|---|
| `0` | `str` | `message` | Heap-allocated, null-terminated error message |
| `8` | `int` | `code` | Numeric code — reserved, always `0` currently |

The typical error-handling idiom is:

```hylian
Error? err = might_fail();
if (err) {
    panic(err.message());
}
```

---

### `Err(msg: str) -> Error?`

Create a new `Error` with the given message. The message string is copied onto the heap. Returns an `Error?` value that compares as truthy (non-nil).

| Parameter | Type | Description |
|---|---|---|
| `msg` | `str` | Human-readable description of the failure |

**Return value:** A heap-allocated `Error?`. Returns `nil` only on out-of-memory (in which case the process is likely already in an unrecoverable state).

**When to use:** Return `Err(...)` from any function whose signature is `-> Error?` to signal that something went wrong.

```hylian
Error? validate_age(int age) {
    if (age < 0) {
        return Err("age cannot be negative");
    }
    if (age > 150) {
        return Err("age is unrealistically large");
    }
    return nil;
}

Error? main() {
    Error? err = validate_age(-5);
    if (err) {
        panic(err.message());
    }
    println("age is valid");
    return nil;
}
```

---

### `panic(msg: str)`

Print `panic: <msg>` to stderr and immediately terminate the process with exit code `1`. **Never returns.**

| Parameter | Type | Description |
|---|---|---|
| `msg` | `str` | Message to print before exiting |

**Return value:** none — this function never returns.

**When to use:** Call `panic` when you encounter an error that the program cannot or should not recover from. It is also the idiomatic way to propagate an `Error?` you do not intend to handle yourself.

```hylian
Error? main() {
    Error? err = some_operation();
    if (err) {
        panic(err.message());
    }
    println("operation succeeded");
    return nil;
}
```

Output on failure:

```sh
panic: <message from some_operation>
```

---

### `Error.message() -> str`

Return the human-readable message string stored in the error.

| Parameter | Type | Description |
|---|---|---|
| *(receiver)* | `Error` | The error to inspect |

**Return value:** A `str` pointing to the heap-allocated message. Do not free this pointer — it is owned by the `Error` object.

```hylian
Error? err = Err("something went wrong");
if (err) {
    str msg = err.message();
    println("Error was: {{msg}}");
}
```

Output:

```sh
Error was: something went wrong
```

---

### `Error.code() -> int`

Return the numeric error code stored in the error.

| Parameter | Type | Description |
|---|---|---|
| *(receiver)* | `Error` | The error to inspect |

**Return value:** An `int` error code. Currently always `0` — this field is reserved for future use and may carry structured codes in a later version of Hylian.

```hylian
Error? err = Err("disk full");
if (err) {
    int code = err.code();
    println("code: {{code}}");
    println("message: {{err.message()}}");
}
```

Output:

```sh
code: 0
message: disk full
```

---

### Full idiom example

A complete example showing a function that returns `Error?`, a caller that inspects the error before deciding whether to panic or continue, and the use of both `Error.message()` and `Error.code()`.

```hylian
include {
    std.io,
}

Error? divide(int a, int b) {
    if (b == 0) {
        return Err("division by zero");
    }
    int result = a / b;
    println("Result: {{result}}");
    return nil;
}

Error? try_parse_and_divide(str input, int len) {
    int n = str_to_int(input, len);
    Error? err = divide(100, n);
    if (err) {
        return err;
    }
    return nil;
}

Error? main() {
    str buf;
    print("Enter a divisor: ");
    int len = read_line(buf, 64);

    Error? err = try_parse_and_divide(buf, len);
    if (err) {
        int code = err.code();
        str msg  = err.message();
        println("Caught error (code {{code}}): {{msg}}");
        panic("cannot continue after failed division");
    }

    println("Done.");
    return nil;
}
```

Example session — success:

```sh
Enter a divisor: 4
Result: 25
Done.
```

Example session — failure:

```sh
Enter a divisor: 0
Caught error (code 0): division by zero
panic: cannot continue after failed division
```
