# Hylian Language Reference

Hylian is a statically typed, transpiled language designed to let you move fast without getting in your way. You write Hylian, it compiles to C++, and then gets compiled by your system's C++ compiler. The philosophy is C#-style ergonomics with Go-style error handling — speed over safety, no borrow checker, no hand-holding.

---

## Table of Contents

1. [Types](#types)
2. [Variables](#variables)
3. [Functions](#functions)
4. [Classes](#classes)
5. [Control Flow](#control-flow)
6. [Operators](#operators)
7. [Error Handling](#error-handling)
8. [Arrays and Multis](#arrays-and-multis)
9. [String Interpolation](#string-interpolation)
10. [Includes](#includes)

---

## Types

Hylian has the following built-in primitive types:

| Hylian type | C++ equivalent     | Notes                        |
|-------------|--------------------|------------------------------|
| `int`       | `int`              |                              |
| `str`       | `std::string`      |                              |
| `bool`      | `bool`             | `true` / `false`             |
| `void`      | `void`             | For functions that return nothing |
| `Error`     | `Error` (struct)   | See [Error Handling](#error-handling) |

### Nullable types

Any type can be made nullable by appending `?`. Nullable types map to a pointer in C++, and can hold `nil` (which maps to `nullptr`).

```hy
Error? err = nil;
```

This is most commonly used as a return type for functions that can fail.

### Custom types

Any class name is a valid type.

```hy
Player p = new Player("Bob", 100);
```

---

## Variables

### Explicit type

```hy
int x;           // declared, uninitialized
int x = 5;       // declared with value
str name = "Bob";
bool alive = true;
```

### Type inference with `:=`

If you don't want to write the type, use `:=`. The type is inferred as `auto` in C++.

```hy
x := 42;
name := "Alice";
err := p.setHealth(100);
```

### Class fields

Fields inside a class default to private. Use `public` or `private` explicitly if you want to be clear.

```hy
public class Player {
    private int health;   // private
    public str name;      // public
    int stamina;          // also private (default)
}
```

---

## Functions

Functions are declared at the top level with a return type, name, and parameter list.

```hy
int add(int a, int b) {
    return a + b;
}

void greet(str name) {
    // ...
}
```

### Entry point

The program entry point is a top-level function named `main`. It should return `Error?` — returning `nil` means success.

```hy
Error? main() {
    // your program starts here
    return nil;
}
```

This compiles to `int main()` in C++. `return nil` becomes `return 0`.

---

## Classes

Classes are defined with the `class` keyword. Use `public` to make the class externally accessible.

```hy
public class Player {
    private int health;
    private str name;

    int getHealth() {
        return health;
    }

    Error? setHealth(int newHealth) {
        if (newHealth < 0) {
            return Err("health cannot be negative");
        }
        health = newHealth;
        return nil;
    }
}
```

### Constructors

A constructor is defined by writing a method with the same name as the class. It must appear **before** the fields and methods in the class body.

```hy
public class Player {
    Player(str n, int h) {
        name = n;
        health = h;
    }
    private str name;
    private int health;

    int getHealth() {
        return health;
    }
}
```

### Instantiation

Use `new ClassName(args)` to create an instance.

```hy
Player p = new Player("Bob", 100);
Player empty = new Player();
```

### Method calls

```hy
int hp = p.getHealth();
p.setHealth(50);
str info = "{{p.getName()}} has {{p.getHealth()}} HP";
```

---

## Control Flow

### If / else

```hy
if (x > 0) {
    // ...
} else {
    // ...
}
```

### While

```hy
while (i < 10) {
    i++;
}
```

### For loop

C-style for loop:

```hy
for (int i = 0; i < 10; i++) {
    // ...
}
```

### Break and continue

```hy
while (true) {
    if (done) {
        break;
    }
    continue;
}
```

### Defer

`defer` runs an expression when the current scope exits, similar to Go.

```hy
defer cleanup();
```

---

## Operators

### Arithmetic

| Operator | Meaning        |
|----------|----------------|
| `+`      | Addition       |
| `-`      | Subtraction    |
| `*`      | Multiplication |
| `/`      | Division       |
| `%`      | Modulo         |

### Comparison

| Operator | Meaning               |
|----------|-----------------------|
| `==`     | Equal                 |
| `!=`     | Not equal             |
| `<`      | Less than             |
| `>`      | Greater than          |
| `<=`     | Less than or equal    |
| `>=`     | Greater than or equal |

### Logical

| Operator | Meaning |
|----------|---------|
| `&&`     | And     |
| `\|\|`   | Or      |
| `!`      | Not     |

### Assignment

| Operator | Meaning              |
|----------|----------------------|
| `=`      | Assign               |
| `:=`     | Declare and assign (type inferred) |
| `+=`     | Add and assign       |
| `-=`     | Subtract and assign  |
| `*=`     | Multiply and assign  |
| `/=`     | Divide and assign    |
| `%=`     | Modulo and assign    |

### Increment / Decrement

```hy
i++;
i--;
++i;
--i;
```

---

## Error Handling

Hylian uses Go-style error handling. Functions that can fail return `Error?`. The caller checks the return value and handles the error explicitly.

```hy
Error? setHealth(int newHealth) {
    if (newHealth < 0) {
        return Err("health cannot be negative");
    }
    health = newHealth;
    return nil;  // nil means success
}
```

Calling a fallible function:

```hy
err := p.setHealth(-1);
if (err) {
    panic(err.message());
}
```

`Err("message")` creates an error value. `nil` means no error. There are no exceptions — errors are just values.

---

## Arrays and Multis

### `array<T>` — typed, flexible size

Maps to `std::vector<T>`. Can grow and shrink at runtime.

```hy
array<int> nums = [1, 2, 3, 4, 5];
array<Enemy> enemies = [goblin, troll];
```

### `array<T, N>` — typed, fixed size

Maps to `std::array<T, N>`. Size is set at compile time.

```hy
array<int, 3> rgb = [255, 128, 0];
```

### `multi<A | B>` — union types, flexible size

Holds elements that can be any of the listed types. Maps to `std::vector<std::variant<A, B>>`.

```hy
multi<str | int> mixed = ["hello", 42, "world", 7];
```

### `multi<A | B, N>` — union types, fixed size

Maps to `std::array<std::variant<A, B>, N>`.

```hy
multi<str | int, 3> trio = ["hello", 42, "world"];
```

### `multi<any>` — any type, flexible size

Holds elements of any type. Maps to `std::vector<std::any>`.

```hy
multi<any> bag = ["hello", 42, true, 3.14];
```

### `multi<any, N>` — any type, fixed size

Maps to `std::array<std::any, N>`.

```hy
multi<any, 2> pair = ["hello", 99];
```

### Indexing

Reading and writing by index works the same for all array and multi types:

```hy
int first = nums[0];
nums[1] = 99;
```

---

## String Interpolation

Embed expressions directly inside string literals using `{{` and `}}`. Any type that can be printed works — `int`, `str`, `float`, method calls, arithmetic, etc.

```hy
str name = "Alice";
int score = 42;
str msg = "Hello {{name}}, your score is {{score}}!";
```

Expressions work too:

```hy
str result = "{{p.getName()}} has {{p.getHealth()}} HP";
str math = "2 + 2 = {{2 + 2}}";
str powers = "{{x}} squared is {{x * x}}";
```

Interpolated strings map to a `std::ostringstream` under the hood, so any streamable type is fair game.

---

## Includes

Use `include` to bring in other Hylian modules. Paths use dot notation relative to the project's source root, and the file extension is omitted.

```hy
include {
    core.string,
    core.io,
    core.errors,
    Game.Player,
    Game.Enemies.Goomba,
}
```

---

## What Hylian Is Not

A few things to be aware of:

- **No type checker (yet)** — type errors are caught by the C++ compiler, not Hylian itself. Error messages may reference generated C++ code.
- **No borrow checker** — memory safety is your responsibility. Reference counting is planned but not yet implemented.
- **No generics on user-defined classes** — `array<T>` and `multi<T>` are built-in. You can't yet write `class List<T>`.
- **No garbage collector** — objects are value types or raw pointers for now.