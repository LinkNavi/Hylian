# Modules and Includes in Hylian

The `include` system is how Hylian programs pull in external declarations — from the standard library, from other source files in your project, and from vendor packages. There is no separate "import" or "use" keyword; `include` is the single mechanism for all module resolution.

---

## Include Syntax

An `include` block lists one or more module paths separated by commas. A trailing comma after the last entry is allowed. The block must appear at the top of the source file, before any declarations.

```hylian
include {
    std.io,
}
```

Multiple modules can be listed in a single block:

```hylian
include {
    std.io,
    std.strings,
}
```

The trailing comma on the last entry is optional but conventional — it makes diffs cleaner when adding or removing modules.

---

## Standard Library Modules

All standard library modules live under the `std` namespace.

| Module | Include Path | Description |
|---|---|---|
| I/O | `std.io` | Console output and input |
| Errors | `std.errors` | Error construction and inspection |
| Strings | `std.strings` | String operations |
| Environment | `std.system.env` | Environment variables, process control |
| Filesystem | `std.system.filesystem` | File read/write, directory utilities |

### std.io

```hylian
include { std.io, }
```

| Function | Signature | Description |
|---|---|---|
| `print` | `fn print(msg: str)` | Print to stdout without a trailing newline |
| `println` | `fn println(msg: str)` | Print to stdout followed by a newline |
| `read_line` | `fn read_line() -> str` | Read one line from stdin, returns empty string on EOF |
| `int_to_str` | `fn int_to_str(n: int) -> str` | Convert an integer to its decimal string representation |
| `str_to_int` | `fn str_to_int(s: str) -> int` | Parse a decimal string to an integer; returns 0 on empty input |

```hylian
include { std.io, }

Error? main() {
    print("Enter your name: ");
    str name = read_line();
    println("Hello, {{name}}!");

    int n = str_to_int("42");
    str s = int_to_str(n + 1);
    println(s);
    return nil;
}
```

---

### std.errors

```hylian
include { std.errors, }
```

`std.errors` is implicitly available in any program that uses `Err()` or `panic()`. Explicit inclusion is only needed if you reference the `Error` class by name.

| Symbol | Signature | Description |
|---|---|---|
| `Err` | `fn Err(msg: str) -> Error?` | Construct an error value |
| `panic` | `fn panic(msg: str)` | Print to stderr and exit with code 1; never returns |
| `Error.message` | `fn message() -> str` | The human-readable error message |
| `Error.code` | `fn code() -> int` | Integer error code (reserved; always 0) |

See [error-handling.md](error-handling.md) for the full error handling reference.

---

### std.strings

```hylian
include { std.strings, }
```

| Function | Signature | Description |
|---|---|---|
| `length` | `fn length(s: str) -> int` | Length of the string |
| `is_empty` | `fn is_empty(s: str) -> bool` | True if null or empty |
| `contains` | `fn contains(s: str, needle: str) -> bool` | True if needle is found |
| `starts_with` | `fn starts_with(s: str, prefix: str) -> bool` | True if starts with prefix |
| `ends_with` | `fn ends_with(s: str, suffix: str) -> bool` | True if ends with suffix |
| `index_of` | `fn index_of(s: str, needle: str) -> int` | First index of needle, or -1 |
| `slice` | `fn slice(s: str, start: int, end: int) -> str` | Substring from start (inclusive) to end (exclusive) |
| `trim` | `fn trim(s: str) -> str` | Remove leading and trailing whitespace |
| `trim_start` | `fn trim_start(s: str) -> str` | Remove leading whitespace |
| `trim_end` | `fn trim_end(s: str) -> str` | Remove trailing whitespace |
| `to_upper` | `fn to_upper(s: str) -> str` | Convert to uppercase |
| `to_lower` | `fn to_lower(s: str) -> str` | Convert to lowercase |
| `replace` | `fn replace(s: str, old: str, new: str) -> str` | Replace all occurrences of old with new |
| `split` | `fn split(s: str, delim: str) -> str[]` | Split by delimiter, returns array of substrings |
| `join` | `fn join(parts: str[], count: int, delim: str) -> str` | Join array of strings with delimiter |
| `to_int` | `fn to_int(s: str, out: int&) -> bool` | Parse decimal string; returns false on failure |
| `to_float` | `fn to_float(s: str, out: float&) -> bool` | Parse float string; returns false on failure |
| `from_int` | `fn from_int(n: int) -> str` | Convert integer to decimal string |
| `equals` | `fn equals(a: str, b: str) -> bool` | True if two strings are equal |

```hylian
include { std.io, std.strings, }

Error? main() {
    str s = "  Hello, World!  ";
    println(trim(s));                      // "Hello, World!"
    println(to_lower(s));                  // "  hello, world!  "
    println(contains(s, "World"));         // true

    str first = slice(s, 2, 7);
    println(first);                        // "Hello"

    str joined = join(split("a,b,c", ","), 3, "-");
    println(joined);                       // "a-b-c"
    return nil;
}
```

---

### std.system.filesystem

```hylian
include { std.system.filesystem, }
```

| Function | Signature | Description |
|---|---|---|
| `file_read` | `fn file_read(path: str, buf: str) -> int` | Read entire file into buf; returns bytes read or -1 |
| `file_write` | `fn file_write(path: str, buf: str) -> int` | Write buf to file, creating/truncating as needed; returns bytes written or -1 |
| `file_append` | `fn file_append(path: str, buf: str) -> int` | Append buf to file; returns bytes written or -1 |
| `file_exists` | `fn file_exists(path: str) -> int` | Returns 1 if file exists, 0 otherwise |
| `file_size` | `fn file_size(path: str) -> int` | Returns file size in bytes, or -1 on error |
| `mkdir` | `fn mkdir(path: str) -> int` | Create directory (mode 0755); returns 0 on success or -1 |
| `getcwd` | `fn getcwd() -> str` | Returns current working directory, or empty string on error |
| `parent_dir` | `fn parent_dir(path: str) -> str` | Returns the parent directory of a path |

```hylian
include { std.io, std.system.filesystem, }

Error? main() {
    int n = file_write("out.txt", "hello\n");
    if (n < 0) {
        return Err("write failed");
    }

    str buf;
    int bytes = file_read("out.txt", buf);
    if (bytes < 0) {
        return Err("read failed");
    }
    println(buf);
    return nil;
}
```

---

### std.system.env

```hylian
include { std.system.env, }
```

| Function | Signature | Description |
|---|---|---|
| `getenv` | `fn getenv(name: str) -> str?` | Returns the environment variable value, or nil if unset |
| `exit` | `fn exit(code: int)` | Terminate the process with the given exit code; never returns |
| `exec` | `fn exec(cmd: str) -> int` | Run a shell command; returns its exit code or -1 |
| `os` | `fn os() -> str` | Returns `"linux"`, `"macos"`, `"windows"`, or `"unknown"` |

```hylian
include { std.io, std.system.env, }

Error? main() {
    str? home = getenv("HOME");
    if (home) {
        println("Home: {{home}}");
    }

    str platform = os();
    if (platform == "linux") {
        exec("uname -a");
    }
    return nil;
}
```

---

> **Crypto and networking** are available as packages via `linkle add`. See the [vendor packages guide](vendors.md).

---

## Module Visibility

When you write a `module` block (typically in a `.hy` file that is included by other files), you can control which symbols are visible to callers by using the `public` modifier.

### Public Functions

Without `public`, a function inside a module is private and cannot be called by code that includes the module:

```hylian
module MathUtils {
    // private — only visible inside this module
    int square(int x) {
        return x * x;
    }

    // public — visible to any file that includes this module
    public int cube(int x) {
        return x * square(x);
    }
}
```

```hylian
include { MathUtils, }

Error? main() {
    int result = MathUtils.cube(3);   // 27
    // MathUtils.square(3);           // error: square is private
    return nil;
}
```

### Public Static Variables

Static variables inside a module are private by default. Add `public` to expose them:

```hylian
module Config {
    public static int max_connections = 100;
    static int internal_counter = 0;    // not visible outside
}
```

### Public Constants

Constants follow the same rule:

```hylian
module Protocol {
    public const int VERSION = 3;
    public const str MAGIC   = "HYL";
    const int INTERNAL_FLAG  = 0xFF;    // not visible outside
}
```

### Public Enums

An enum declared inside a module must be `public` to be accessible by callers:

```hylian
module Status {
    public enum Code {
        OK      = 0,
        NotFound = 1,
        Error   = 2,
    }
}
```

```hylian
include { Status, }

Error? main() {
    Status.Code result = Status.Code.OK;
    return nil;
}
```

> **Coming from Go?** This mirrors Go's capitalised-name export rule, but explicit. In Hylian you write `public` instead of capitalising the symbol name.

