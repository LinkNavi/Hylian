# `runtime`

Source: `stdlib/runtime.hy`. **Always linked** into every compiled program, same as
`mem` — not something you `include` yourself.

Provides the two low-level primitives the compiler calls directly for the
`print`/`println` **syntax** (as opposed to `io`'s `print()`/`println()` *functions*,
an ordinary stdlib layer built on `syscall()` that is never actually invoked by the
`print`/`println` statement forms — `compiler/ir_to_mir.c`'s print/println lowering
emits calls named exactly `hylian_print`/`hylian_println` regardless of whether `io`
is even included).

| Function | Signature | Description |
|---|---|---|
| `hylian_print` | `naked void hylian_print(usize str_ptr, usize len)` | Writes `len` bytes at `str_ptr` to fd 1. |
| `hylian_println` | `naked void hylian_println(usize str_ptr, usize len)` | Same, followed by one newline byte. |

`hylian_println` writes its trailing newline from a 1-byte local rather than a `"\n"`
string literal, because the compiler never processes backslash escapes in string
literals (see [Syntax: String Literals](../language/syntax.md#string-and-character-literals))
— `"\n"` would actually be the two raw bytes `\` and `n`. Storing the byte value `10`
in a local and writing 1 byte from its address sidesteps that.

This file deliberately does not `include platform.linux_x86_64`, for the same
translation-unit-collision reason described in [`mem`](mem.md).
