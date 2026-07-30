# `string`

```hylian
include { string, }
```

Source: `stdlib/string.hy`. Written as plain functions (`trim(s)`, not `s.trim()`) —
Hylian doesn't have method-call/UFCS sugar on primitive types yet, and every function
here takes the string as its first parameter specifically so that `s.trim()` could
become sugar for `trim(s)` later without changing this file.

| Function | Signature | Description |
|---|---|---|
| `length` | `int length(str s)` | Number of bytes before the terminating nul. |
| `is_empty` | `bool is_empty(str s)` | True if `s` is `nil` or has zero length. |
| `equals` | `bool equals(str a, str b)` | Byte-for-byte comparison. |
| `index_of` | `int index_of(str s, str needle)` | Index of the first occurrence of `needle`, or `-1`. |
| `contains` | `bool contains(str s, str needle)` | True if `needle` occurs anywhere in `s`. |
| `starts_with` | `bool starts_with(str s, str prefix)` | True if `s` begins with `prefix`. |
| `ends_with` | `bool ends_with(str s, str suffix)` | True if `s` ends with `suffix`. |
| `slice` | `str slice(str s, int start, int end)` | Substring `[start, end)`. Out-of-range indices are clamped, not errors. |
| `trim_start` | `str trim_start(str s)` | Remove leading whitespace (space, tab, `\n`, `\r`). |
| `trim_end` | `str trim_end(str s)` | Remove trailing whitespace. |
| `trim` | `str trim(str s)` | Remove leading and trailing whitespace. |
| `to_upper` | `str to_upper(str s)` | ASCII-only uppercase conversion; returns a new string. |
| `to_lower` | `str to_lower(str s)` | ASCII-only lowercase conversion; returns a new string. |
| `concat` | `str concat(str a, str b)` | New string containing `a` followed by `b`. |

There is no `split`, `join`, `replace`, `to_int`/`to_float`/`from_int` in the current
`string.hy` — those appeared in older drafts of this documentation but don't exist in
the source today.

```hylian
include { io, string, }

int main() {
    str s = "  Hello, World!  ";
    println(trim(s));
    println(to_lower(s));
    return 0;
}
```

All of these allocate their result via `raw_alloc` (from `platform.linux_x86_64`) —
see [Known Limitations](../language/known-limitations.md#printprintln-only-reliably-handle-string-literals)
regarding printing the string these functions return.
