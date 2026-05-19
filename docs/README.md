# Hylian

## What is Hylian?

Hylian is a compiled systems programming language with C-like syntax that compiles directly to x86-64 NASM assembly. It is statically typed, has no garbage collector, and is designed for performance-critical software — giving you full control over memory without sacrificing readability. Hylian features clean, first-class error handling, a straightforward class system, a native FFI vendor system, and a growing standard library targeting Linux, macOS, and Windows.

---

## Quick Start

### Building the Compiler

Clone the repository and build the `hylian` compiler binary from source:

```sh
./build.sh
```

The compiler binary will be placed at the root of the repository as `./hylian`. The compiler source lives in `compiler/`.

The build script accepts optional flags:
- `--clean` — remove build artifacts before building
- `--verbose` / `-v` — show each compilation command
- `--skip-runtime` — skip rebuilding the runtime standard library objects

### Creating a Project

Use `linkle` to scaffold a new project:

```sh
linkle new my-project
cd my-project
```

This creates the following structure:

```
my-project/
├── linkle.hy       ← project manifest
├── src/
│   └── main.hy     ← entry point
├── vendors/        ← vendor packages go here
└── .gitignore
```

### Building and Running

```sh
linkle build        # compile and link
linkle run          # run the default run() target
```

Or invoke the compiler manually for a single file:

```sh
./hylian hello.hy -o hello.asm
nasm -f elf64 hello.asm -o hello.o
gcc hello.o runtime/std/io.o -o hello -no-pie
./hello
```

**Compiler flags:**

| Flag | Description |
|---|---|
| `-o <output.asm>` | Output file (default: `output.asm`) |
| `--src-dir <dir>` | Directory to resolve includes from |
| `--target <linux\|macos\|windows\|limine>` | Compilation target (default: `linux`) |
| `--freestanding` | Freestanding/kernel mode (no stdlib) |
| `--dump-ir` | Print IR before and after optimization to stderr |

### Hello, World!

```hylian
include {
    std.io,
}

Error? main() {
    println("Hello, World!");
    return nil;
}
```

---

## Language at a Glance

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
| Enum | `enum Color { Red, Green, Blue }` |
| Enum with values | `enum Flag { Read = 1, Write = 2 }` |
| Enum access | `Color.Red` |
| Switch | `switch (x) { case 1: { ... } default: { ... } }` |
| If / else | `if (x > 0) { ... } else { ... }` |
| While loop | `while (i < 10) { i++; }` |
| C-style for loop | `for (int i = 0; i < 5; i++) { ... }` |
| For-in loop | `for (n in nums) { ... }` |
| For-in by reference | `for (&n in nums) { *n = *n * 2; }` |
| Increment / decrement | `i++`, `++i`, `i--`, `--i` |
| Compound assignment | `x += 1`, `x -= 1`, `x %= 10` |
| Heap array | `array<int> nums = [1, 2, 3];` |
| Array length | `nums.len` |
| Array push / pop | `nums.push(4)`, `int v = nums.pop()` |
| Static fixed array | `static int buf[256];` |
| Const global | `const int MAX = 100;` |
| Tagged union | `multi<int \| str> x = 42;` |
| Union tag | `x.tag` (0-based index) |
| C-style union | `union class Reg { public uint64 qword; public uint32 dword; }` |
| Tuple / multi-return | `(int, int) divmod(int a, int b) { return a/b, a%b; }` |
| Reference type | `&int ref = &x;` |
| Raw pointer type | `*uint32 ptr = cast<*uint32>(addr);` |
| Address-of | `&x` |
| Dereference | `*ptr` |
| Unsafe block | `unsafe { *ptr = 0xFF; }` |
| Include modules | `include { std.io, std.strings, }` |
| Include vendor package | `include { vendors.mylib, }` |
| Inline assembly | `asm { ... }` |
| Nullable type | `int? x = nil;` |
| Exit program | `exit(0);` |

---

## Standard Library

| Module | Include Path | Description |
|---|---|---|
| I/O | `std.io` | Console input and output: `println`, `print`, `read_line` |
| Errors | `std.errors` | Error construction and inspection utilities |
| Strings | `std.strings` | String manipulation: split, join, trim, find, replace, etc. |
| Environment | `std.system.env` | Environment variables, process control |
| Filesystem | `std.system.filesystem` | File read/write, directory utilities, path helpers |

---

## Build System — `linkle`

`linkle` is the project build tool. It reads `linkle.hy` at the project root.

### `linkle.hy` Manifest

```
project {
    name: "my-project",
    version: "0.1.0",
    author: "you",
    description: "...",    // optional
    repo: "...",           // optional
}

build {
    src: "src",                  // source directory
    main: "main",                // entry point file (without .hy)
    out: "build",                // build output root
    bin: "my-project",           // binary name (defaults to project name)
    target: "linux",             // optional: linux|macos|windows|kernel|limine
    linker_script: "my.ld",      // optional: for freestanding/kernel targets
}

// Vendor packages — maps an include alias to a directory under vendors/
vendors {
    mylib: "vendors/mylib",
    vulkan: "vendors/vulkan",
}

target run() {
    exec("./build/bin/my-project");
}

target clean() {
    exec("rm -rf build");
}
```

### Build Output Layout

```
build/
├── bin/
│   └── my-project     ← final executable
└── obj/
    ├── main.asm
    ├── main.o
    └── vendors/
        └── mylib.o    ← cached vendor object file
```

### Commands

| Command | Description |
|---|---|
| `linkle new <name>` | Scaffold a new project |
| `linkle new-workspace <name>` | Scaffold a new workspace with kernel + init members |
| `linkle build` | Compile and link the project |
| `linkle run` | Build and execute the `run()` target |
| `linkle <target>` | Execute any named target from `linkle.hy` |
| `linkle vendor new <name>` | Scaffold a new vendor package under `vendors/` |
| `linkle add <name[@ver]>` | Download a package from the registry |
| `linkle publish` | Publish this package to the registry |
| `linkle login <token>` | Save a registry API token |
| `linkle update [--channel nightly]` | Update the toolchain |
| `linkle use <version>` | Pin to a specific toolchain version |

---

## Workspaces

A workspace groups multiple related packages (e.g. a kernel and a userspace init process) under a single root. Create one with:

```sh
linkle new-workspace my-os
cd my-os
```

The workspace root `linkle.hy` uses a `workspace` block instead of a `build` block:

```
workspace {
    members: ["kernel", "init"],
}
```

Each member directory contains its own `linkle.hy` with its own `project` and `build` blocks. Running `linkle build` from the workspace root builds all members in order.

---

## Vendor Packages

The `vendors/` directory holds native library wrappers and pure-Hylian packages. A vendor package consists of:

- A `.hyi` interface file declaring the `module`, `link` directive, and symbol signatures
- An optional `.hy` file with pure-Hylian helper code

```
vendors/
└── mylib/
    ├── mylib.hyi    ← native .so wrapper
    └── mylib.hy     ← optional pure-Hylian helpers
```

Declare packages in `linkle.hy` and include them in source:

```hylian
include {
    std.io,
    vendors.mylib,
}
```

See [language/vendors.md](language/vendors.md) for the full reference, including `.hyi` format, class declarations, and worked examples for Vulkan and OpenGL.

---

## Documentation

Full language and standard library documentation lives in the `docs/` directory:

- **[language/syntax.md](language/syntax.md)** — Types, variables, functions, classes, control flow, enums, switch, arrays, pointers, tuples, inline assembly, and more
- **[language/error-handling.md](language/error-handling.md)** — The `Error?` type, `Err()`, `panic`, propagation patterns
- **[language/modules.md](language/modules.md)** — Include system, full stdlib reference, module visibility (`public`)
- **[language/vendors.md](language/vendors.md)** — Vendor/FFI system, `.hyi` format, Vulkan and OpenGL examples
- **[language/kernel.md](language/kernel.md)** — Freestanding/kernel mode, the `kernel` module, GDT/IDT, bare-metal examples

---

## Project Structure

```
hylian/
├── hylian              # Compiled compiler binary (after build)
├── linkle.py           # Build tool source
├── compiler/           # Compiler source code (C, lex, bison)
│   ├── lexer.l         # Flex lexer
│   ├── parser.y        # Bison grammar
│   ├── ast.c / ast.h   # Abstract syntax tree definitions
│   ├── ir.c / ir.h     # Intermediate representation
│   ├── lower.c / lower.h  # IR lowering pass
│   ├── opt.c / opt.h   # IR optimization pass
│   ├── typecheck.c / typecheck.h  # Type checker
│   ├── codegen_asm.c / codegen_asm.h  # x86-64 NASM code generator
│   └── compiler.c      # Compiler entry point
├── runtime/
│   └── std/            # Standard library source and precompiled .o files
├── docs/
│   ├── README.md       # This file
│   ├── language/       # Language reference documentation
│   └── stdlib/         # Standard library documentation
└── tests/              # Test programs and test harness
```
