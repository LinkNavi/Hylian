# `runtime`

Source: `stdlib/runtime.hy`. **Always linked** into every compiled program, same as
`mem` — not something you `include` yourself.

Provides the low-level primitives the compiler calls directly. They are ordinary functions
but exempt from the compiler's automatic per-function arena, and none of them is meant to be
called from user code.

| Function | Signature | Description |
|---|---|---|
| `hylian_print` / `hylian_println` | `void hylian_print(usize str_ptr, usize len)` | The `print`/`println` **syntax**: write `len` bytes to fd 1 (`println` adds a newline). Note this is distinct from `io`'s `print()`/`println()` *functions*. |
| `hylian_int_to_str` | `int hylian_int_to_str(int val, usize buf, int buflen)` | Decimal text of an integer; used by `println(someInt)`. |
| `hylian_float_to_str` | `int hylian_float_to_str(float x, usize buf, int buflen)` | Text of a float (up to 6 fractional digits, trailing zeros trimmed, `e+N` for huge values, `nan`/`inf`); used by `println(someFloat)`. |
| `hylian_str_alloc` | `usize hylian_str_alloc(int n)` | Zeroed bytes for a string, carved from never-freed 64 KiB chunks. |
| `hylian_str_concat` | `usize hylian_str_concat(usize a, usize b)` | `a + b` for strings (`nil` counts as `""`); backs `+`, `+=` and interpolation. |
| `hylian_str_from_int` / `_bool` / `_float` | `usize hylian_str_from_int(int v)` | Convert a value to a string; used by interpolation. |
| `hylian_panic` | `void hylian_panic(usize msg)` | `panic("msg")`: write `panic: msg` to stderr and exit 1. |

This file deliberately does not `include platform.linux_x86_64`, for the same
translation-unit-collision reason described in [`mem`](mem.md).
