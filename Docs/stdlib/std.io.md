## std.io

Input and output functions for reading from stdin and writing to stdout.

```hylian
include {
    std.io,
}
```

---

### Functions

#### `println(msg: str)`

Print a string to stdout followed by a newline.

| Parameter | Type | Description |
|---|---|---|
| `msg` | `str` or `int` | The value to print |

**Return value:** none

> **Compiler note:** `println` is handled specially by the compiler. You may pass an `int` directly — it will be auto-converted to its decimal string representation. String interpolation with `{{var}}` is also supported inside string literals.

```hylian
include {
    std.io,
}

Error? main() {
    println("Hello, world!");

    int count = 7;
    println(count);

    println("There are {{count}} items.");

    return nil;
}
```

---

#### `print(msg: str)`

Print a string to stdout with no trailing newline.

| Parameter | Type | Description |
|---|---|---|
| `msg` | `str` or `int` | The value to print |

**Return value:** none

> **Compiler note:** Like `println`, `print` accepts an `int` directly and supports `{{var}}` interpolation in string literals.

```hylian
include {
    std.io,
}

Error? main() {
    print("Enter your name: ");

    str buf;
    int len = read_line(buf, 128);

    print("Hello, ");
    println(buf);

    return nil;
}
```

---

#### `read_line(buf: str, buflen: int) -> int`

Read one line from stdin into `buf`. Strips the trailing newline (`\n` or `\r\n`). Uses an internal 4096-byte buffer for efficient reads.

| Parameter | Type | Description |
|---|---|---|
| `buf` | `str` | Caller-supplied buffer to write the line into |
| `buflen` | `int` | Maximum number of bytes to write into `buf` |

**Return value:** Number of bytes written into `buf`. Returns `0` on EOF. Returns `0` if `buf` is null or `buflen` is less than or equal to `0`.

```hylian
include {
    std.io,
}

Error? main() {
    print("Enter a number: ");

    str buf;
    int len = read_line(buf, 64);

    if (len == 0) {
        println("Got EOF.");
        return nil;
    }

    int n = str_to_int(buf, len);
    println("You entered: {{n}}");

    return nil;
}
```

---

#### `int_to_str(n: int, buf: str, buflen: int) -> int`

Convert an integer to its decimal string representation, writing the result into `buf`.

| Parameter | Type | Description |
|---|---|---|
| `n` | `int` | The integer to convert |
| `buf` | `str` | Caller-supplied buffer to write the string into |
| `buflen` | `int` | Size of `buf` in bytes |

**Return value:** Number of characters written. Returns `0` if `buf` is null, `buflen` is less than or equal to `0`, or `buf` is too small to hold the result (a 64-bit integer needs at most 21 bytes including a potential minus sign).

> **Note:** In most cases you can pass an `int` directly to `print` or `println` and the compiler will handle the conversion automatically. Use `int_to_str` when you need the string representation stored in a buffer for further processing.

```hylian
include {
    std.io,
}

Error? main() {
    int score = 9001;

    str buf;
    int len = int_to_str(score, buf, 32);

    print("Score as string (");
    print(len);
    print(" chars): ");
    println(buf);

    return nil;
}
```

---

#### `str_to_int(s: str, len: int) -> int`

Parse a decimal integer from the first `len` bytes of string `s`. Skips any leading whitespace and accepts an optional leading `+` or `-` sign. Stops at the first non-digit character after the sign.

| Parameter | Type | Description |
|---|---|---|
| `s` | `str` | The string to parse |
| `len` | `int` | Number of bytes of `s` to consider |

**Return value:** The parsed integer. Returns `0` for empty input, whitespace-only input, or if `s` is null or `len` is less than or equal to `0`. Note that `0` is also a valid parse result — use `hylian_to_int` from `std.strings` if you need to distinguish between a parse failure and a genuine zero.

```hylian
include {
    std.io,
}

Error? main() {
    print("Enter a number: ");

    str buf;
    int len = read_line(buf, 64);

    int n = str_to_int(buf, len);
    int doubled = n * 2;

    println("Double that is: {{doubled}}");

    return nil;
}
```

---

### String interpolation with `println`

String literals passed to `print` or `println` support `{{var}}` interpolation. Any in-scope variable name can appear between the double braces and will be substituted with its string value at runtime. Integer variables are automatically converted.

```hylian
include {
    std.io,
}

Error? main() {
    int x = 42;
    str name = "world";

    println("Hello, {{name}}!");
    println("The answer is {{x}}");
    println("Double the answer is {{x}}");

    return nil;
}
```

Output:

```sh
Hello, world!
The answer is 42
Double the answer is 42
```
