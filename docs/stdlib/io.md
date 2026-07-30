# `io`

```hylian
include { io, }
```

Source: `stdlib/io.hy`. Basic terminal I/O over raw `read()`/`write()` syscalls (via
`platform.linux_x86_64`, which `io` includes itself).

| Function | Signature | Description |
|---|---|---|
| `print` | `void print(str msg)` | Write `msg` to stdout, no trailing newline. |
| `eprint` | `void eprint(str msg)` | Write `msg` to stderr, no trailing newline. |
| `println` | `void println(str msg)` | `print(msg)` followed by a newline byte. |
| `eprintln` | `void eprintln(str msg)` | `eprint(msg)` followed by a newline byte. |
| `int_to_str` | `str int_to_str(int n)` | Decimal string representation of `n`, handles negative values. |
| `str_to_int` | `int str_to_int(str s)` | Parse a decimal string (optional leading whitespace, optional `+`/`-`). Returns `0` for empty or whitespace-only input. |
| `read_line` | `str read_line()` | Read one line from stdin (byte at a time), without the trailing newline. Returns an empty string at EOF. Not meant for bulk input — see `os.fs`'s `read_all` for that. |

```hylian
include { io, }

int main() {
    print("Enter your name: ");
    str name = read_line();
    println(name);
    return 0;
}
```

Note that `print`/`eprint`/`println`/`eprintln` here are ordinary stdlib functions
built on `syscall()`, distinct from the compiler's own `print`/`println` *statement*
syntax (which lowers directly to `hylian_print`/`hylian_println` in
`stdlib/runtime.hy` and is always linked in, whether or not `io` is included). See
[Known Limitations](../language/known-limitations.md#printprintln-only-reliably-handle-string-literals)
— today, only a string *literal* argument to either form of print/println is verified
reliable; printing a `str` produced at runtime (including this module's own
`int_to_str`) was observed to crash.
