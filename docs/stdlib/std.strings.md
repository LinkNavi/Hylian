## std.strings

String inspection, transformation, and conversion functions.

```hylian
include {
    std.strings,
}
```

> **Note:** Functions in `std.strings` are currently called by their full C ABI names (`hylian_length`, `hylian_contains`, etc.) directly from Hylian source. No prefix rewriting is applied for this module.

> **Memory note:** Functions marked *heap-allocated* return a newly allocated string. The caller is responsible for freeing it. This will be handled automatically by a future garbage collector or `defer` statement.

---

### Functions

#### `hylian_length(s: str) -> int`

Return the length of a string in bytes.

| Parameter | Type | Description |
|---|---|---|
| `s` | `str` | The string to measure |

**Return value:** Byte count of `s`, not including the null terminator. Returns `0` for an empty string.

```hylian
include {
    std.io,
    std.strings,
}

Error? main() {
    str word = "Hylian";
    int len = hylian_length(word);
    println("Length: {{len}}");
    return nil;
}
```

Output:

```sh
Length: 6
```

---

#### `hylian_is_empty(s: str) -> int`

Check whether a string is null or empty.

| Parameter | Type | Description |
|---|---|---|
| `s` | `str` | The string to check |

**Return value:** `1` if `s` is null or has zero length, `0` otherwise.

```hylian
include {
    std.io,
    std.strings,
}

Error? main() {
    str empty = "";
    str full  = "hello";

    println(hylian_is_empty(empty));
    println(hylian_is_empty(full));

    return nil;
}
```

Output:

```sh
1
0
```

---

#### `hylian_contains(s: str, needle: str) -> int`

Check whether `needle` appears anywhere inside `s`.

| Parameter | Type | Description |
|---|---|---|
| `s` | `str` | The string to search within |
| `needle` | `str` | The substring to look for |

**Return value:** `1` if `needle` is found, `0` otherwise.

```hylian
include {
    std.io,
    std.strings,
}

Error? main() {
    str sentence = "the quick brown fox";
    println(hylian_contains(sentence, "brown"));
    println(hylian_contains(sentence, "cat"));
    return nil;
}
```

Output:

```sh
1
0
```

---

#### `hylian_starts_with(s: str, prefix: str) -> int`

Check whether `s` begins with `prefix`.

| Parameter | Type | Description |
|---|---|---|
| `s` | `str` | The string to test |
| `prefix` | `str` | The expected prefix |

**Return value:** `1` if `s` starts with `prefix`, `0` otherwise.

```hylian
include {
    std.io,
    std.strings,
}

Error? main() {
    str path = "/home/link/readme.txt";
    println(hylian_starts_with(path, "/home"));
    println(hylian_starts_with(path, "/tmp"));
    return nil;
}
```

Output:

```sh
1
0
```

---

#### `hylian_ends_with(s: str, suffix: str) -> int`

Check whether `s` ends with `suffix`.

| Parameter | Type | Description |
|---|---|---|
| `s` | `str` | The string to test |
| `suffix` | `str` | The expected suffix |

**Return value:** `1` if `s` ends with `suffix`, `0` otherwise.

```hylian
include {
    std.io,
    std.strings,
}

Error? main() {
    str filename = "readme.txt";
    println(hylian_ends_with(filename, ".txt"));
    println(hylian_ends_with(filename, ".md"));
    return nil;
}
```

Output:

```sh
1
0
```

---

#### `hylian_index_of(s: str, needle: str) -> int`

Find the byte index of the first occurrence of `needle` in `s`.

| Parameter | Type | Description |
|---|---|---|
| `s` | `str` | The string to search within |
| `needle` | `str` | The substring to find |

**Return value:** Zero-based byte index of the first match. Returns `-1` if `needle` is not found.

```hylian
include {
    std.io,
    std.strings,
}

Error? main() {
    str s = "hello world";
    int i = hylian_index_of(s, "world");
    int j = hylian_index_of(s, "zzz");

    println("world at: {{i}}");
    println("zzz at:   {{j}}");

    return nil;
}
```

Output:

```sh
world at: 6
zzz at:   -1
```

---

#### `hylian_slice(s: str, start: int, end: int) -> str`

Return a heap-allocated copy of the substring `s[start..end)` (start inclusive, end exclusive).

| Parameter | Type | Description |
|---|---|---|
| `s` | `str` | The source string |
| `start` | `int` | Start byte index (clamped to `0` if negative) |
| `end` | `int` | End byte index, exclusive (clamped to `hylian_length(s)` if beyond the end) |

**Return value:** *Heap-allocated* substring. Returns an empty heap string if `start >= end`. Returns `nil` on out-of-memory. **Caller must free.**

```hylian
include {
    std.io,
    std.strings,
}

Error? main() {
    str s = "Hello, Hylian!";
    str sub = hylian_slice(s, 7, 13);
    println(sub);
    return nil;
}
```

Output:

```sh
Hylian
```

---

#### `hylian_trim(s: str) -> str`

Strip leading and trailing whitespace from `s`.

| Parameter | Type | Description |
|---|---|---|
| `s` | `str` | The string to trim |

**Return value:** *Heap-allocated* trimmed copy. Returns an empty heap string if `s` is all whitespace. **Caller must free.**

```hylian
include {
    std.io,
    std.strings,
}

Error? main() {
    str padded = "   hello   ";
    str trimmed = hylian_trim(padded);
    println(trimmed);
    return nil;
}
```

Output:

```sh
hello
```

---

#### `hylian_trim_start(s: str) -> str`

Strip leading (left-side) whitespace from `s`.

| Parameter | Type | Description |
|---|---|---|
| `s` | `str` | The string to trim |

**Return value:** *Heap-allocated* copy with leading whitespace removed. **Caller must free.**

```hylian
include {
    std.io,
    std.strings,
}

Error? main() {
    str s = "   indented";
    str result = hylian_trim_start(s);
    println(result);
    return nil;
}
```

Output:

```sh
indented
```

---

#### `hylian_trim_end(s: str) -> str`

Strip trailing (right-side) whitespace from `s`.

| Parameter | Type | Description |
|---|---|---|
| `s` | `str` | The string to trim |

**Return value:** *Heap-allocated* copy with trailing whitespace removed. **Caller must free.**

```hylian
include {
    std.io,
    std.strings,
}

Error? main() {
    str s = "trailing spaces   ";
    str result = hylian_trim_end(s);
    println("'{{result}}'");
    return nil;
}
```

Output:

```sh
'trailing spaces'
```

---

#### `hylian_to_upper(s: str) -> str`

Return an uppercase copy of `s`.

| Parameter | Type | Description |
|---|---|---|
| `s` | `str` | The string to convert |

**Return value:** *Heap-allocated* uppercase copy. **Caller must free.**

```hylian
include {
    std.io,
    std.strings,
}

Error? main() {
    str s = "Hello, World!";
    str up = hylian_to_upper(s);
    println(up);
    return nil;
}
```

Output:

```sh
HELLO, WORLD!
```

---

#### `hylian_to_lower(s: str) -> str`

Return a lowercase copy of `s`.

| Parameter | Type | Description |
|---|---|---|
| `s` | `str` | The string to convert |

**Return value:** *Heap-allocated* lowercase copy. **Caller must free.**

```hylian
include {
    std.io,
    std.strings,
}

Error? main() {
    str s = "Hello, World!";
    str low = hylian_to_lower(s);
    println(low);
    return nil;
}
```

Output:

```sh
hello, world!
```

---

#### `hylian_replace(s: str, old: str, new: str) -> str`

Return a copy of `s` with every occurrence of `old` replaced by `new`.

| Parameter | Type | Description |
|---|---|---|
| `s` | `str` | The source string |
| `old` | `str` | The substring to replace |
| `new` | `str` | The replacement substring |

**Return value:** *Heap-allocated* copy with all replacements applied. If `old` is empty, returns a copy of `s` unchanged. Returns `nil` on out-of-memory. **Caller must free.**

```hylian
include {
    std.io,
    std.strings,
}

Error? main() {
    str s = "foo bar foo baz foo";
    str result = hylian_replace(s, "foo", "qux");
    println(result);
    return nil;
}
```

Output:

```sh
qux bar qux baz qux
```

---

#### `hylian_split(s: str, delim: str) -> str[]`

Split `s` on every occurrence of `delim`, returning a null-terminated array of heap-allocated substrings.

| Parameter | Type | Description |
|---|---|---|
| `s` | `str` | The string to split |
| `delim` | `str` | The delimiter to split on |

**Return value:** *Heap-allocated* null-terminated array of `str` pointers. The last element is `nil`. Each element and the array itself must be freed by the caller. Returns `nil` on out-of-memory.

> If `delim` does not appear in `s`, the result is an array of one element containing a copy of the whole string.

```hylian
include {
    std.io,
    std.strings,
}

Error? main() {
    str csv = "apple,banana,cherry";
    str[] parts = hylian_split(csv, ",");

    int i = 0;
    while (parts[i]) {
        println(parts[i]);
        i = i + 1;
    }

    return nil;
}
```

Output:

```sh
apple
banana
cherry
```

---

#### `hylian_join(parts: str[], count: int, delim: str) -> str`

Join an array of strings into a single string, with `delim` inserted between each element.

| Parameter | Type | Description |
|---|---|---|
| `parts` | `str[]` | Array of strings to join |
| `count` | `int` | Number of elements in `parts` |
| `delim` | `str` | Delimiter to insert between elements |

**Return value:** *Heap-allocated* joined string. Returns an empty heap string if `count` is `0`. Returns `nil` on out-of-memory. **Caller must free.**

```hylian
include {
    std.io,
    std.strings,
}

Error? main() {
    str[] words = ["one", "two", "three"];
    str joined = hylian_join(words, 3, " - ");
    println(joined);
    return nil;
}
```

Output:

```sh
one - two - three
```

---

#### `hylian_to_int(s: str, out: int*) -> int`

Parse a decimal integer from `s` and write the result into `out`.

| Parameter | Type | Description |
|---|---|---|
| `s` | `str` | The string to parse |
| `out` | `int*` | Pointer to an `int` that receives the parsed value |

**Return value:** `1` on success, `0` if the string is not a valid integer or if any non-numeric characters remain after the number. Unlike `str_to_int` from `std.io`, this function distinguishes a parse failure from a genuine zero result.

```hylian
include {
    std.io,
    std.strings,
}

Error? main() {
    int value;
    int ok = hylian_to_int("123", value);

    if (ok) {
        println("Parsed: {{value}}");
    } else {
        println("Parse failed.");
    }

    ok = hylian_to_int("not_a_number", value);
    if (ok == 0) {
        println("Correctly rejected bad input.");
    }

    return nil;
}
```

Output:

```sh
Parsed: 123
Correctly rejected bad input.
```

---

#### `hylian_to_float(s: str, out: float*) -> int`

Parse a floating-point number from `s` and write the result into `out`.

| Parameter | Type | Description |
|---|---|---|
| `s` | `str` | The string to parse |
| `out` | `float*` | Pointer to a `float` that receives the parsed value |

**Return value:** `1` on success, `0` if the string is not a valid floating-point number or if any characters remain after the number.

```hylian
include {
    std.io,
    std.strings,
}

Error? main() {
    float f;
    int ok = hylian_to_float("3.14", f);

    if (ok) {
        println("Parsed float successfully.");
    } else {
        println("Parse failed.");
    }

    return nil;
}
```

---

#### `hylian_from_int(n: int) -> str`

Convert an integer to its heap-allocated decimal string representation.

| Parameter | Type | Description |
|---|---|---|
| `n` | `int` | The integer to convert |

**Return value:** *Heap-allocated* null-terminated decimal string. Returns `nil` on out-of-memory. **Caller must free.**

> **Note:** For simple printing, passing an `int` directly to `print` or `println` is more convenient — the compiler handles the conversion automatically. Use `hylian_from_int` when you need to store or manipulate the string value itself.

```hylian
include {
    std.io,
    std.strings,
}

Error? main() {
    int n = 42;
    str s = hylian_from_int(n);
    str msg = hylian_replace("the answer is X", "X", s);
    println(msg);
    return nil;
}
```

Output:

```sh
the answer is 42
```

---

#### `hylian_equals(a: str, b: str) -> int`

Compare two strings for exact byte-for-byte equality.

| Parameter | Type | Description |
|---|---|---|
| `a` | `str` | First string |
| `b` | `str` | Second string |

**Return value:** `1` if `a` and `b` are identical, `0` otherwise.

> **Note:** Do not use `==` to compare strings in Hylian — that compares pointer addresses, not contents. Always use `hylian_equals` for string equality checks.

```hylian
include {
    std.io,
    std.strings,
}

Error? main() {
    str a = "hello";
    str b = "hello";
    str c = "world";

    println(hylian_equals(a, b));
    println(hylian_equals(a, c));

    return nil;
}
```

Output:

```sh
1
0
```
