# Hylian

## What is Hylian?

Hylian is a compiled systems programming language with C-like syntax that compiles directly to x86-64 NASM assembly. It is statically typed, has no garbage collector, and is designed for performance-critical software — giving you full control over memory without sacrificing readability. Hylian features clean, first-class error handling, a straightforward class system, and a growing standard library targeting Linux, macOS, and Windows.

---

## Quick Start

### Building the Compiler

Clone the repository and build the `hylian` compiler binary from source:

```sh
cd src
bison -d parser.y
flex lexer.l
gcc lex.yy.c parser.tab.c ast.c codegen_asm.c typecheck.c compiler.c -o ../hylian
```

The compiler binary will be placed at the root of the repository as `./hylian`.

### Hello, World!

Create a file called `hello.hy`:

```hylian
include {
    std.io,
}

Error? main() {
    println("Hello, World!");
    return nil;
}
```

Compile and run it:

```sh
./hylian hello.hy -o hello.asm
nasm -f elf64 hello.asm -o hello.o
gcc hello.o runtime/std/io.o -o hello -no-pie
./hello
```

> **Note:** The build tool `linkle` (coming soon) will automate the `nasm` and `gcc` linking steps.

---

## Language at a Glance

A quick reference for Hylian syntax.

| Feature | Syntax |
|---|---|
| Integer variable | `int x = 42;` |
| String variable | `str name = "Alice";` |
| Boolean | `bool flag = true;` |
| Float | `float pi = 3.14;` |
| String interpolation | `"Hello, {{name}}!"` |
| Function | `int add(int a, int b) { return a + b; }` |
| Error-returning function | `Error? save(str path) { ... return nil; }` |
| Return an error | `return Err("something went wrong");` |
| Check an error | `if (err) { panic(err.message()); }` |
| Class definition | `public class Player { ... }` |
| Constructor | `Player(str n, int h) { name = n; health = h; }` |
| Instantiation | `Player p = new Player("Bob", 100);` |
| If / else | `if (x > 0) { ... } else { ... }` |
| While loop | `while (i < 10) { i = i + 1; }` |
| C-style for loop | `for (int i = 0; i < 5; i = i + 1) { ... }` |
| For-in loop | `for (n in nums) { ... }` |
| Heap array | `array<int> nums = [1, 2, 3];` |
| Array length | `nums.len` |
| Tagged union | `multi<int \| str> x = 42;` |
| Union tag | `x.tag` (0-based index) |
| Include modules | `include { std.io, std.crypto, }` |
| Inline assembly | `asm { ... }` |
| Nullable type | `int? x = nil;` |

---

## Standard Library

| Module | Include Path | Description |
|---|---|---|
| I/O | `std.io` | Console input and output, `println`, `print`, `readln` |
| Errors | `std.errors` | Error construction helpers and error utilities |
| Strings | `std.strings` | String manipulation: split, join, trim, find, replace, etc. |
| Crypto | `std.crypto` | Hashing and basic cryptographic primitives |
| Environment | `std.system.env` | Read and write environment variables |
| Filesystem | `std.system.filesystem` | File read/write, directory traversal, path utilities |
| TCP Networking | `std.networking.tcp` | Low-level TCP socket client and server support |
| UDP Networking | `std.networking.udp` | UDP datagram send and receive |
| HTTPS | `std.networking.https` | High-level HTTPS client for making web requests |

---

## Documentation

Full language and standard library documentation lives in the `Docs/` directory:

- **[Docs/language/](language/)** — Language reference: syntax, types, error handling, modules, and more.
- **[Docs/stdlib/](stdlib/)** — Standard library reference for each module.

---

## Project Structure

```
hylian/
├── hylian              # Compiled compiler binary (after build)
├── src/                # Compiler source code (C, lex, bison)
│   ├── lexer.l         # Flex lexer
│   ├── parser.y        # Bison grammar
│   ├── ast.c / ast.h   # Abstract syntax tree definitions
│   ├── typecheck.c     # Type checker
│   ├── codegen_asm.c   # x86-64 NASM code generator
│   └── compiler.c      # Compiler entry point
├── runtime/
│   └── std/            # Precompiled standard library object files (.o)
├── Docs/
│   ├── README.md       # This file
│   ├── language/       # Language reference documentation
│   └── stdlib/         # Standard library documentation
└── tests/              # Test programs and test harness
```
