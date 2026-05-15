# std.system.filesystem

File and directory operations for Hylian programs.

```hylian
include {
    std.system.filesystem,
}
```

> **Note:** Path strings do **not** need to be null-terminated — every function accepts a separate `path_len` byte-length argument. All length limits are enforced at 4096 bytes.

---

## Functions

### `hylian_file_read(path, path_len, buf, buf_len) -> int`

Read the entire contents of a file into a buffer.

Opens the file in binary mode, reads up to `buf_len` bytes, and closes the file. If the file is larger than `buf_len`, only the first `buf_len` bytes are returned — no error is reported for truncation.

| Parameter  | Type  | Description                                  |
|------------|-------|----------------------------------------------|
| `path`     | `str` | Path to the file to read                     |
| `path_len` | `int` | Byte length of `path`                        |
| `buf`      | `str` | Buffer to read the file contents into        |
| `buf_len`  | `int` | Capacity of `buf` in bytes                   |

**Returns:** the number of bytes read, or `-1` on error (file not found, permission denied, etc.).

```hylian
include {
    std.system.filesystem,
    std.io,
}

Error? main() {
    array<int, 4096> buf;
    int n = hylian_file_read("config.txt", 10, buf, 4096);
    if (n < 0) {
        println("Could not read config.txt");
        return nil;
    }
    println("Read {{n}} bytes: {{buf}}");
    return nil;
}
```

---

### `hylian_file_write(path, path_len, buf, buf_len) -> int`

Write a buffer to a file, creating the file if it does not exist and truncating it if it does.

| Parameter  | Type  | Description                                  |
|------------|-------|----------------------------------------------|
| `path`     | `str` | Path to the file to write                    |
| `path_len` | `int` | Byte length of `path`                        |
| `buf`      | `str` | Data to write                                |
| `buf_len`  | `int` | Number of bytes to write from `buf`          |

**Returns:** the number of bytes written, or `-1` on error (permission denied, invalid path, etc.).

```hylian
include {
    std.system.filesystem,
    std.io,
}

Error? main() {
    str content = "Hello, Hylian!";
    int n = hylian_file_write("output.txt", 10, content, 14);
    if (n < 0) {
        println("Write failed");
        return nil;
    }
    println("Wrote {{n}} bytes to output.txt");
    return nil;
}
```

---

### `hylian_file_append(path, path_len, buf, buf_len) -> int`

Append a buffer to the end of a file, creating the file if it does not exist.

Unlike `hylian_file_write`, this function never truncates an existing file — it always writes at the end.

| Parameter  | Type  | Description                                  |
|------------|-------|----------------------------------------------|
| `path`     | `str` | Path to the file to append to                |
| `path_len` | `int` | Byte length of `path`                        |
| `buf`      | `str` | Data to append                               |
| `buf_len`  | `int` | Number of bytes to append from `buf`         |

**Returns:** the number of bytes written, or `-1` on error.

```hylian
include {
    std.system.filesystem,
    std.io,
}

Error? main() {
    str entry = "2024-01-15 startup\n";
    int n = hylian_file_append("app.log", 7, entry, 19);
    if (n < 0) {
        println("Could not write to app.log");
        return nil;
    }
    println("Log entry written ({{n}} bytes)");
    return nil;
}
```

---

### `hylian_file_exists(path, path_len) -> int`

Check whether a file exists at the given path.

| Parameter  | Type  | Description               |
|------------|-------|---------------------------|
| `path`     | `str` | Path to check             |
| `path_len` | `int` | Byte length of `path`     |

**Returns:** `1` if the file exists and can be opened, `0` if it does not exist or cannot be accessed.

```hylian
include {
    std.system.filesystem,
    std.io,
}

Error? main() {
    int exists = hylian_file_exists("config.txt", 10);
    if (exists == 1) {
        println("config.txt found");
    } else {
        println("config.txt not found — using defaults");
    }
    return nil;
}
```

---

### `hylian_file_size(path, path_len) -> int`

Get the size of a file in bytes.

| Parameter  | Type  | Description               |
|------------|-------|---------------------------|
| `path`     | `str` | Path to the file          |
| `path_len` | `int` | Byte length of `path`     |

**Returns:** the file size in bytes, or `-1` if the file does not exist or cannot be opened.

```hylian
include {
    std.system.filesystem,
    std.io,
}

Error? main() {
    int size = hylian_file_size("data.bin", 8);
    if (size < 0) {
        println("Could not stat data.bin");
        return nil;
    }
    println("data.bin is {{size}} bytes");
    return nil;
}
```

> **Tip:** Call `hylian_file_size` before `hylian_file_read` to allocate exactly the right buffer size.

---

### `hylian_mkdir(path, path_len) -> int`

Create a directory at the given path.

On POSIX systems the directory is created with mode `0755`. On Windows the platform default is used. This function does **not** create intermediate parent directories — all parents must already exist.

| Parameter  | Type  | Description                             |
|------------|-------|-----------------------------------------|
| `path`     | `str` | Path of the directory to create         |
| `path_len` | `int` | Byte length of `path`                   |

**Returns:** `0` on success, or `-1` on failure (directory already exists, permission denied, parent does not exist, etc.).

```hylian
include {
    std.system.filesystem,
    std.io,
}

Error? main() {
    int rc = hylian_mkdir("build", 5);
    if (rc < 0) {
        println("Could not create build/ (may already exist)");
    } else {
        println("build/ created");
    }
    return nil;
}
```

---

## Complete Example

Check that a configuration file exists, read it, log the operation, and write a processed result to a new directory.

```hylian
include {
    std.system.filesystem,
    std.io,
}

Error? main() {
    // 1. Make sure the source file exists before trying to read it.
    int exists = hylian_file_exists("input.txt", 9);
    if (exists == 0) {
        println("input.txt not found — nothing to do");
        return nil;
    }

    // 2. Check how large it is so we know if our buffer is big enough.
    int size = hylian_file_size("input.txt", 9);
    if (size < 0) {
        println("Could not determine file size");
        return nil;
    }
    println("input.txt is {{size}} bytes");

    // 3. Read the file contents.
    array<int, 65536> content;
    int n = hylian_file_read("input.txt", 9, content, 65536);
    if (n < 0) {
        println("Read failed");
        return nil;
    }
    println("Read {{n}} bytes");

    // 4. Create an output directory for the results.
    int rc = hylian_mkdir("out", 3);
    if (rc < 0) {
        // Directory may already exist — not necessarily fatal.
        println("Note: could not create out/ (may already exist)");
    }

    // 5. Write the content to the output file.
    int written = hylian_file_write("out/result.txt", 14, content, n);
    if (written < 0) {
        println("Write to out/result.txt failed");
        return nil;
    }
    println("Wrote {{written}} bytes to out/result.txt");

    // 6. Append a log entry.
    str log_entry = "processed input.txt -> out/result.txt\n";
    hylian_file_append("out/run.log", 11, log_entry, 38);

    println("Done.");
    return nil;
}
```
