# Hylian

## What is Hylian?

Hylian is a compiled systems programming language with C-like syntax, statically
typed, with no garbage collector, targeting Linux/x86-64. It compiles through its own
IR and a MIR-based backend (`compiler/ir_to_mir.c` + `compiler/codegen_hygen.c`)
straight to a linkable ELF64 object — there's no separate textual-assembly step or
`nasm` invocation in the current compiler.

This documentation set is grounded directly in the compiler source
(`compiler/parser.y`, `compiler/lexer.l`, `compiler/ast.h`, `compiler/typecheck.c`,
`compiler/lower.c`, `compiler/ir_to_mir.c`) and the current `stdlib/` tree, verified by
actually compiling, linking, and running the examples where practical. Where a
feature parses and typechecks but doesn't yet work end-to-end, that's called out
explicitly — see [Known Limitations](language/known-limitations.md) — rather than
glossed over.

**Read that page first.** The single biggest finding from this pass: in an ordinary
(non-`naked`) function, using a function call's result any later than the very next
statement — including simply `return`-ing it — is unreliable, because of a
register-allocation bug in the automatic per-function arena cleanup. It's explained
in full, with a minimal reproduction, at the top of
[Known Limitations](language/known-limitations.md).

---

## Quick Start

### Building the Compiler

```sh
./build.sh
```

The compiler binary lands at the repo root as `./hylian`. Flags: `--clean` (remove
build artifacts first), `--verbose`/`-v` (show each compile command),
`--skip-runtime` (skip rebuilding stdlib objects).

### Compiling a Program Directly

```sh
./hylian hello.hy -o hello.o --src-dir stdlib
gcc hello.o -o hello -no-pie
./hello
```

`-o` now names an **object file** (`output.o` by default) — `hylian` emits a linkable
ELF64 object directly; there is no `.asm`-then-`nasm` step in the current pipeline.
`--src-dir` tells the compiler where to resolve `include` paths from; point it at
`stdlib/` (or a directory containing your own modules plus a copy of/symlink to
`stdlib/`) so standard library includes resolve. Two stdlib translation units —
`mem` (the arena allocator behind `new`) and `runtime` (the builtins behind the
`print`/`println` syntax) — are **always** needed at link time regardless of what your
program includes:

```sh
./hylian stdlib/mem.hy -o mem.o --src-dir stdlib
./hylian stdlib/runtime.hy -o runtime.o --src-dir stdlib
gcc hello.o mem.o runtime.o -o hello -no-pie
```

**Compiler flags:**

| Flag | Description |
|---|---|
| `-o <output.o>` | Output object file (default: `output.o`) |
| `--src-dir <dir>` | Directory to resolve `include` paths from |
| `--target <linux\|macos\|windows\|limine\|termina>` | Compilation target (default: `linux`) — only `linux` is currently exercised end-to-end |
| `--freestanding` | Freestanding/kernel mode (no stdlib) |
| `--dump-ir` | Print IR before and after optimization to stderr |

### Hello, World!

```hylian
include {
    io,
}

Error? main() {
    println("Hello, World!");
    return nil;
}
```

This exact program compiles, links, and runs correctly against the current compiler.

### The `linkle` Build Tool

`linkle` (`linkle.py`) is the project's build tool and scaffolder — it reads a
`linkle.hy` manifest and is meant to wrap the two-line `hylian` + `gcc` invocation
above. **As of this writing, `linkle build`/`linkle run`'s default (`linux`-target)
pipeline is broken**: it still compiles to a `.asm` path and shells out to `nasm`, but
the current `hylian` writes a real ELF object to whatever `-o` path it's given
regardless of the extension, so `nasm` ends up trying to assemble object-code bytes as
text and fails with garbled syntax errors. This is a known, tracked gap (the project's
own `TODO` file lists rewriting `linkle`'s build pipeline as outstanding work) — use
the direct `hylian`/`gcc` invocation above until it's fixed. `linkle new`/
`linkle new-workspace` (scaffolding) still work fine; it's specifically the compile
step inside `build`/`run` that's stale.

```sh
linkle new my-project
cd my-project
```

```
my-project/
├── linkle.hy       ← project manifest
├── src/
│   └── main.hy     ← entry point
├── vendors/        ← vendor packages go here
└── .gitignore
```

`linkle.hy` manifest shape:

```
project {
    name: "my-project",
    version: "0.1.0",
    author: "you",
}

build {
    src: "src",
    main: "main",
    out: "build",
    bin: "my-project",
    target: "linux",
}

vendors {
    mylib: "vendors/mylib",
}

target run() {
    exec("./build/bin/my-project");
}
```

| Command | Description |
|---|---|
| `linkle new <name>` | Scaffold a new project |
| `linkle new-workspace <name>` | Scaffold a workspace (multiple packages under one root) |
| `linkle build` | Compile and link the project (linux-target pipeline currently broken, see above) |
| `linkle run` | Build and execute the `run()` target |
| `linkle <target>` | Execute any named target from `linkle.hy` |
| `linkle vendor new <name>` | Scaffold a new vendor package under `vendors/` |
| `linkle add <name[@version]>` | Download a package from the registry |
| `linkle publish` | Publish this package to the registry |
| `linkle login <token>` | Save a registry API token |
| `linkle update [--channel nightly]` | Update the toolchain |
| `linkle use <version>` | Pin to a specific toolchain version |

---

## Language at a Glance

| Feature | Syntax | Status |
|---|---|---|
| Integer / string / bool / float variable | `int x = 42;` `str s = "hi";` `bool b = true;` `float f = 3.14;` | works |
| Type-inferred declaration | `x := 10;` | works |
| Function | `int add(int a, int b) { return a + b; }` | control flow/args work; **using a call's result later than the very next statement is unreliable** — [details](language/known-limitations.md) |
| Error-returning function | `Error? save(str path) { ... return nil; }` | `Error?`/`nil` works; see below |
| Construct/inspect an error | `Err("message")`, `err.message()` | **does not link** — [details](language/known-limitations.md) |
| `panic` | `panic(err.message());` | **does not link** — [details](language/known-limitations.md) |
| Class + constructor + instantiation | `public class Player { ... }`, `new Player(...)` | zero-field classes work; **fields don't link** |
| Enum | `enum Color { Red, Green, Blue }` | works; `.` member access **doesn't link** |
| Switch / if-else / while | `switch`/`if`/`else`/`while` | works |
| C-style `for` | `for (int i = 0; i < 5; i = i + 1) { ... }` | works — **not** `i++` as the post-clause, see [syntax](language/syntax.md#for) |
| `for`-`in` / by reference | `for (n in nums) { ... }`, `for (&n in nums) { ... }` | blocked on `array<T>`, see below |
| Increment / decrement | `i++`, `++i`, `i--`, `--i` | **parses but doesn't mutate** — [details](language/known-limitations.md) |
| Compound assignment | `x += 1` (identifier targets only) | works |
| Heap array | `array<int> nums = [1, 2, 3];` | **does not link** — [details](language/known-limitations.md) |
| Tagged union | `multi<int \| str> x = 42;` | **does not link** — [details](language/known-limitations.md) |
| C-style union | `union class Reg { public uint64 qword; public uint32 dword; }` | works |
| Reference / raw pointer | `&int r = &x;`, `*uint32 p = cast<*uint32>(addr);` | works |
| Unsafe block | `unsafe { *ptr = 0xFF; }` | works |
| `include` | `include { io, string, }` | works |
| Inline assembly | `asm { ... }` (body can't contain a literal `}`) | works |
| Nullable type | `int? x = nil;` | works |
| `print`/`println` | `println("literal");` | string literals work; non-literal args are unreliable — [details](language/known-limitations.md) |

See [Known Limitations](language/known-limitations.md) for exactly how each gap above
was verified.

---

## Standard Library

| Module | Include Path | Description |
|---|---|---|
| I/O | `io` | `println`, `print`, `read_line`, `int_to_str`, `str_to_int` |
| Strings | `string` | `trim`, `slice`, `contains`, `to_upper`/`to_lower`, `concat`, etc. |
| Memory | `mem` | Arena allocator behind `new` — always linked |
| Runtime | `runtime` | Builtins behind the `print`/`println` syntax — always linked |
| Networking | `net` | Raw IPv4/TCP sockets |
| Time | `time` | Wall-clock time, `sleep` |
| Process control | `os.exec` | `fork`, `exec`, `spawn`, `wait`, `kill` |
| Filesystem | `os.fs` | `open`/`read`/`write`, `read_all`/`write_all`, `mkdir`, `cwd`, `pipe`, ... |
| Mounting / power | `os.mount` | `mount`, `reboot`, `poweroff`, `halt` |
| User identity | `os.user` | `uid`, `gid`, `is_root` |
| Platform | `platform.linux_x86_64` | Syscall numbers, flag constants, `raw_alloc`/`raw_free` |

Full reference: [stdlib docs](stdlib/README.md). Note this is a flat, syscall-based
`stdlib/` tree — not a `std.*`-namespaced set. See
[Modules and Includes](language/modules.md) for path-resolution details.

---

## Vendor Packages

The `vendors/` directory holds native library wrappers and pure-Hylian packages —
covered in full in [Vendor Packages & FFI](language/vendors.md).

```hylian
include {
    io,
    vendors.mylib,
}
```

---

## Documentation

- **[language/README.md](language/README.md)** — Language Reference index
- **[language/syntax.md](language/syntax.md)** — Full grammar reference
- **[language/error-handling.md](language/error-handling.md)** — `Error?`, `Err()`, `panic`
- **[language/modules.md](language/modules.md)** — `include` system, stdlib module list, visibility
- **[language/known-limitations.md](language/known-limitations.md)** — Verified gaps between the grammar and the current backend
- **[language/vendors.md](language/vendors.md)** — FFI / vendor package system
- **[language/kernel.md](language/kernel.md)** — Freestanding/kernel-mode development
- **[stdlib/](stdlib/README.md)** — Per-module standard library reference

---

## Project Structure

```
hylian/
├── hylian              # Compiled compiler binary (after build)
├── linkle.py           # Build tool source
├── compiler/            # Compiler source (C, flex, bison)
│   ├── lexer.l          # Flex lexer
│   ├── parser.y         # Bison grammar
│   ├── ast.c / ast.h    # AST definitions
│   ├── ir.c / ir.h      # Intermediate representation
│   ├── lower.c / lower.h    # AST → IR lowering
│   ├── opt.c / opt.h        # IR optimization
│   ├── typecheck.c / typecheck.h
│   ├── ir_to_mir.c / ir_to_mir.h  # IR → MIR lowering
│   ├── codegen_hygen.c / codegen_hygen.h  # MIR → x86-64 ELF64 object
│   └── compiler.c       # Entry point
├── stdlib/              # The current, pure-Hylian standard library
│   ├── io.hy, mem.hy, net.hy, runtime.hy, string.hy, time.hy
│   ├── os/              # exec.hy, fs.hy, mount.hy, user.hy
│   └── platform/        # linux_x86_64.hy
├── runtime/              # Older C runtime tree, partially superseded by stdlib/
├── docs/
│   ├── README.md         # This file
│   ├── language/         # Language reference documentation
│   └── stdlib/           # Standard library documentation
└── tests/                # Test programs and test harness
```
