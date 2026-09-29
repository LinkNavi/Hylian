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
| `char_at` | `int char_at(str s, int i)` | Byte at index `i` as `0`–`255`, or `-1` if out of range. |
| `last_index_of` | `int last_index_of(str s, str needle)` | Index of the last occurrence of `needle`, or `-1`. |
| `index_of_from` | `int index_of_from(str s, str needle, int from)` | Like `index_of`, searching from byte offset `from`; the result indexes into `s`. |
| `split` | `array<str> split(str s, str delim)` | Pieces between each `delim`. `split("a,b,,c", ",")` → `["a","b","","c"]`; adjacent delimiters give empty strings, `split("", ",")` gives `[""]`, an empty `delim` gives `[s]`. |
| `join` | `str join(array<str> parts, str sep)` | Concatenate `parts` with `sep` between each pair. |
| `replace` | `str replace(str s, str from, str to)` | Every occurrence of `from` replaced by `to` (an empty `from` returns a copy). |
| `repeat` | `str repeat(str s, int n)` | `s` repeated `n` times (`""` for `n <= 0`). |
| `format` | `str format(str fmt, array<str> args)` | Substitute each `{}` in `fmt` with the next element of `args`. Leftover `{}` stay as-is; extra args are ignored. No format specifiers — convert numbers with `int_to_str` first (there are no variadics). |
| `parse_int` | `int parse_int(str s, int fallback)` | Optionally-signed decimal integer; `fallback` if `s` is empty or not entirely digits. |
| `parse_hex` | `int parse_hex(str s, int fallback)` | Unprefixed hex integer (either case); `fallback` on bad input. |

```hylian
include { io, string, }

int main() {
    array<str> parts = split("GET /index.html HTTP/1.1", " ");
    println(parts[1]);                                       // /index.html
    println(join(parts, "|"));                               // GET|/index.html|HTTP/1.1
    println(format("{} has {} items", ["cart", int_to_str(3)]));
    return 0;
}
```

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
