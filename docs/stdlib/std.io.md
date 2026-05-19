## std.io

Input and output functions for reading from stdin and writing to stdout.

```hylian
include {
    std.io,
}
```

---

### Functions

#### `print(msg: str)`

Print a string to stdout with no trailing newline.

| Parameter | Type | Description |
|---|---|---|
| `msg` | `str` or `int` | The value to print |

**Return value:** none

> **Compiler note:** `print` is handled specially by the compiler. You may pass an `int` directly — it will be auto-converted to its decimal string representation. String interpolation with `{{var}}` is also supported inside string literals.

```hylian
include {
    std.io,
}

Error? main() {
    print("Enter your name: ");
    str name = read_line();
    print("Hello, ");
    println(name);

    return nil;
}
```

---

#### `println(msg: str)`

Print a string to stdout followed by a newline.

| Parameter | Type | Description |
|---|---|---|
| `msg` | `str` or `int` | The value to print |

**Return value:** none

> **Compiler note:** Like `print`, `println` accepts an `int` directly and supports `{{var}}` interpolation in string literals.

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

#### `read_line() -> str`

Read one line from stdin and return it as a `str`. Strips the trailing newline (`\n` or `\r\n`). Returns an empty string on EOF.

**Parameters:** none

**Return value:** The line read from stdin as a `str`, without the trailing newline. Returns an empty string on EOF.

```hylian
include {
    std.io,
}

Error? main() {
    print("Enter your name: ");
    str name = read_line();

    if (name == "") {
        println("Got EOF.");
        return nil;
    }

    println("Hello, {{name}}!");

    return nil;
}
```

---

#### `int_to_str(n: int) -> str`

Convert an integer to its decimal string representation and return it as a `str`.

| Parameter | Type | Description |
|---|---|---|
| `n` | `int` | The integer to convert |

**Return value:** The decimal string representation of `n`.

> **Note:** In most cases you can pass an `int` directly to `print` or `println` and the compiler will handle the conversion automatically. Use `int_to_str` when you need the string representation for further processing — for example, to concatenate it with another string or store it in a variable.

```hylian
include {
    std.io,
}

Error? main() {
    int score = 9001;
    str score_str = int_to_str(score);

    println("Your score is: " + score_str);
    println("Length of score string: {{score_str}}");

    return nil;
}
```

---

#### `str_to_int(s: str) -> int`

Parse a decimal integer from the string `s`. Skips any leading whitespace and accepts an optional leading `+` or `-` sign. Stops at the first non-digit character after the sign.

| Parameter | Type | Description |
|---|---|---|
| `s` | `str` | The string to parse |

**Return value:** The parsed integer. Returns `0` for empty input or whitespace-only input. Note that `0` is also a valid parse result — use `hylian_to_int` from `std.strings` if you need to distinguish between a parse failure and a genuine zero.

```hylian
include {
    std.io,
}

Error? main() {
    print("Enter a number: ");
    str input = read_line();

    int n = str_to_int(input);
    int doubled = n * 2;

    println("Double that is: {{doubled}}");

    return nil;
}
```

---

### String interpolation with `print` and `println`

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

    return nil;
}
```

Output:

```sh
Hello, world!
The answer is 42
```
