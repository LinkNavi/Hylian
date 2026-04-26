# Hylian Language Syntax Reference

This document is the comprehensive reference for Hylian syntax and language features. For error handling patterns see [error-handling.md](error-handling.md), and for the module system see [modules.md](modules.md).

---

## Types

### Primitive Types

| Type | Size | Description |
|---|---|---|
| `int` | 64-bit | Signed integer |
| `str` | pointer | String (pointer to null-terminated character data) |
| `bool` | 8-bit | Boolean value: `true` or `false` |
| `float` | 64-bit | Double-precision floating point number |
| `void` | — | No value. Used as a return type when a function returns nothing, or with `nil` |

### Nullable Types

Any type can be made nullable by appending `?`. A nullable type can hold the value `nil` in addition to its normal range of values.

```hylian
int? count = nil;
str? name = "Alice";
Error? err = nil;
```

The most common nullable type in Hylian is `Error?`, used as a return type for functions that may fail. `nil` means success; a non-nil value means an error occurred.

### Class Types

User-defined classes are types. An instance is created with `new`:

```hylian
Player p = new Player("Bob", 100);
```

### `array<T>`

A heap-allocated, dynamically resizable array of elements of type `T`. Arrays have two built-in fields:

- `.len` — the number of elements currently stored
- `.cap` — the total allocated capacity

```hylian
array<int> nums = [1, 2, 3];
int length = nums.len;
int capacity = nums.cap;
```

An empty array declaration pre-allocates 8 slots:

```hylian
array<str> names;
```

### `multi<A | B | ...>`

A tagged union that can hold a value of any one of the listed types at a time. Two built-in fields are available:

- `.tag` — a 0-based integer index indicating which type is currently stored
- `.value` — the stored value

```hylian
multi<int | str> x = 42;
int tag = x.tag;   // 0 = int, 1 = str
int val = x.value;

multi<int | str> y = "hello";
// y.tag == 1
```

`multi<any>` is an untyped union that can hold a value of any type without specifying the possible variants up front.

---

## Variables

Variables are declared with their type first, followed by the name and an optional initializer. All statements are terminated with a semicolon.

```hylian
int x = 42;
str name = "Alice";
bool flag = true;
float pi = 3.14;
```

A variable can be declared without an initializer, in which case it holds a zero or nil value depending on its type:

```hylian
int count;
array<str> names;
```

---

## String Interpolation

Hylian supports inline variable interpolation inside string literals using double curly braces `{{varname}}`. The variable is converted to its string representation at runtime.

```hylian
int score = 99;
println("Your score is {{score}}.");

str player = "Alice";
int health = 75;
println("{{player}} has {{health}} HP remaining.");
```

---

## Functions

### Declaration

The return type is written first, followed by the function name and its parameter list. The function body is enclosed in braces.

```hylian
int add(int a, int b) {
    return a + b;
}

str greet(str name) {
    return "Hello, {{name}}!";
}

void logMessage(str msg) {
    println(msg);
}
```

### Error-Returning Functions

Functions that may fail return `Error?`. Return `nil` to signal success, or `Err("message")` to signal failure.

```hylian
Error? save(str path) {
    // ... write to disk ...
    return nil;
}

Error? validateAge(int age) {
    if (age < 0) {
        return Err("age cannot be negative");
    }
    return nil;
}
```

### Calling Functions

```hylian
int result = add(3, 4);
str msg = greet("Bob");
Error? err = save("/tmp/data.txt");
```

---

## Classes

Classes are declared with the `public class` keywords. Fields are declared inside the class body with an access modifier (`private` or `public`). The constructor has the same name as the class and no return type.

```hylian
public class Player {
    private str name;
    private int health;

    Player(str n, int h) {
        name = n;
        health = h;
    }

    str getName() {
        return name;
    }

    int getHealth() {
        return health;
    }

    Error? setHealth(int h) {
        if (h < 0) {
            return Err("health cannot be negative");
        }
        health = h;
        return nil;
    }
}
```

### Instantiation

Use `new` to create an instance:

```hylian
Player p = new Player("Bob", 100);
```

### Calling Methods

```hylian
str name = p.getName();
int hp = p.getHealth();

Error? err = p.setHealth(80);
if (err) {
    panic(err.message());
}
```

---

## Error Handling

See [error-handling.md](error-handling.md) for the full reference. A brief summary:

```hylian
Error? doThing() {
    return Err("it broke");
}

Error? err = doThing();
if (err) {
    panic(err.message());
}
```

- `Err("message")` — constructs an error value
- `nil` — represents no error (success)
- `err.message()` — returns the error message as a `str`
- `err.code()` — returns an `int` error code (currently always `0`)
- `panic(msg)` — prints `msg` to stderr and exits with code 1; never returns

---

## Control Flow

### If / Else

```hylian
if (x > 0) {
    println("positive");
} else {
    println("non-positive");
}
```

Chained conditions:

```hylian
if (x > 0) {
    println("positive");
} else if (x < 0) {
    println("negative");
} else {
    println("zero");
}
```

### While Loop

```hylian
int i = 0;
while (i < 10) {
    println(i);
    i = i + 1;
}
```

### C-Style For Loop

```hylian
for (int i = 0; i < 5; i = i + 1) {
    println(i);
}
```

### For-In Loop

Iterates over every element of an `array<T>`:

```hylian
array<int> nums = [1, 2, 3, 4, 5];
for (n in nums) {
    println(n);
}
```

### Break and Continue

`break` exits the nearest enclosing loop immediately. `continue` skips the rest of the current iteration and proceeds to the next. Both are supported in `while`, C-style `for`, and `for-in` loops.

```hylian
for (int i = 0; i < 10; i = i + 1) {
    if (i == 3) {
        continue;
    }
    if (i == 7) {
        break;
    }
    println(i);
}
```

---

## Arrays

### Literal Initialization

```hylian
array<int> nums = [10, 20, 30];
array<str> words = ["hello", "world"];
```

### Empty Array

Declares an array with no elements and 8 slots pre-allocated:

```hylian
array<str> names;
```

### Indexing

Indices are 0-based:

```hylian
int first = nums[0];
nums[1] = 99;
```

### Length and Capacity

```hylian
int len = nums.len;
int cap = nums.cap;
```

### Iterating

```hylian
array<int> nums = [10, 20, 30];
for (n in nums) {
    println(n);
}
```

---

## Multi (Tagged Union)

`multi<A | B | ...>` holds exactly one value whose type is one of the listed variants. The `.tag` field is a 0-based integer indicating the active variant, and `.value` holds the current value.

```hylian
multi<int | str> x = 42;
int tag = x.tag;    // 0 = int, 1 = str
int val = x.value;

multi<int | str> y = "hello";
// y.tag == 1
// y.value == "hello"
```

Use `x.tag` to branch on which variant is active:

```hylian
if (x.tag == 0) {
    println("it's an int");
} else {
    println("it's a str");
}
```

`multi<any>` is an untyped variant that places no constraints on what type the value may be:

```hylian
multi<any> val = 123;
```

---

## Inline Assembly

The `asm { ... }` block allows embedding raw x86-64 NASM assembly directly in a Hylian function. This is an advanced feature intended for low-level programming where direct hardware or register access is required.

```hylian
void cpuid() {
    asm {
        mov eax, 0
        cpuid
    }
}
```

The assembly is emitted verbatim into the generated `.asm` output. No register saving or calling-convention handling is performed automatically — the programmer is responsible for preserving any registers that Hylian's code generator may depend on.

---

## Modules

Use `include` to import modules before using their declarations. See [modules.md](modules.md) for the full reference.

```hylian
include {
    std.io,
    std.crypto,
    std.networking.https,
}
```

Multiple modules can appear in a single `include` block, separated by commas. A trailing comma after the last entry is allowed.