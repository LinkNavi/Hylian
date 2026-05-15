# std.system.env

Environment variable access and process control for Hylian programs.

```hylian
include {
    std.system.env,
}
```

---

## Functions

### `hylian_getenv(name, name_len, buf, buf_len) -> int`

Look up an environment variable by name and copy its value into a buffer.

`name` does **not** need to be null-terminated — pass the string and its byte length separately.

| Parameter  | Type  | Description                                      |
|------------|-------|--------------------------------------------------|
| `name`     | `str` | The name of the environment variable to look up  |
| `name_len` | `int` | Byte length of `name`                            |
| `buf`      | `str` | Buffer to copy the variable's value into         |
| `buf_len`  | `int` | Size of `buf` in bytes                           |

**Returns:** the number of bytes copied into `buf`, or `-1` if the variable was not found or any argument is invalid.

> **Note:** The value is copied without a null terminator. Use the returned length to know how many bytes are valid in `buf`.

```hylian
include {
    std.system.env,
    std.io,
}

Error? main() {
    array<int, 512> buf;

    int n = hylian_getenv("HOME", 4, buf, 512);
    if (n < 0) {
        println("HOME is not set");
    } else {
        println("Home directory: {{buf}}");
    }

    return nil;
}
```

---

### `hylian_exit(code) -> void`

Terminate the process immediately with the given exit code. This function does not return.

By convention, `0` signals success and any non-zero value signals failure.

| Parameter | Type  | Description                                  |
|-----------|-------|----------------------------------------------|
| `code`    | `int` | Exit code to pass to the operating system    |

**Returns:** nothing — the process is terminated.

```hylian
include {
    std.system.env,
    std.io,
}

Error? main() {
    array<int, 512> buf;

    int n = hylian_getenv("APP_ENV", 7, buf, 512);
    if (n < 0) {
        println("APP_ENV is not set — aborting");
        hylian_exit(1);
    }

    println("Running in environment: {{buf}}");
    return nil;
}
```

---

## Practical Example

Read both `HOME` and `PATH`, report which are set, and exit with a summary code.

```hylian
include {
    std.system.env,
    std.io,
}

Error? main() {
    array<int, 512> home_buf;
    array<int, 4096> path_buf;

    int missing = 0;

    int home_len = hylian_getenv("HOME", 4, home_buf, 512);
    if (home_len < 0) {
        println("HOME  : not set");
        missing = missing + 1;
    } else {
        println("HOME  : {{home_buf}}");
    }

    int path_len = hylian_getenv("PATH", 4, path_buf, 4096);
    if (path_len < 0) {
        println("PATH  : not set");
        missing = missing + 1;
    } else {
        println("PATH  : {{path_buf}}");
    }

    if (missing > 0) {
        println("Warning: {{missing}} required variable(s) missing.");
        hylian_exit(1);
    }

    println("Environment OK.");
    hylian_exit(0);

    return nil;
}
```
