# Modules and Includes in Hylian

The `include` system is how Hylian programs pull in external declarations — both from the standard library and from user-defined source files. There is no separate "import" or "use" keyword; `include` is the single mechanism for all module resolution.

---

## Include Syntax

An `include` block lists one or more module paths separated by commas. A trailing comma after the last entry is allowed. The block must appear at the top of the source file, before any declarations.

```hylian
include {
    std.io,
}
```

Multiple modules can be listed in a single block:

```hylian
include {
    std.io,
    std.strings,
    std.crypto,
}
```

The trailing comma on the last entry is optional but conventional — it makes diffs cleaner when adding or removing modules.

---

## Standard Library Modules

All standard library modules live under the `std` namespace. Their include paths use dot notation.

| Module | Include Path | Description |
|---|---|---|
| I/O | `std.io` | Console output and input: `println`, `print`, `readln` |
| Errors | `std.errors` | Error construction helpers and error inspection utilities |
| Strings | `std.strings` | String operations: split, join, trim, find, replace, and more |
| Crypto | `std.crypto` | Hashing functions and basic cryptographic primitives |
| Environment | `std.system.env` | Read and write process environment variables |
| Filesystem | `std.system.filesystem` | File read/write, directory traversal, and path utilities |
| TCP | `std.networking.tcp` | Low-level TCP socket client and server support |
| UDP | `std.networking.udp` | UDP datagram send and receive |
| HTTPS | `std.networking.https` | High-level HTTPS client for making web requests |

Example — including several standard library modules at once:

```hylian
include {
    std.io,
    std.system.filesystem,
    std.networking.https,
}
```

---

## User Modules

You can include your own `.hy` source files using the same dot notation. Dots in the module path are treated as directory separators, and the path is resolved relative to the source directory.

```hylian
include {
    Game.Player,
}
```

This resolves to `Game/Player.hy` relative to the source root. Deeper nesting works the same way:

```hylian
include {
    Game.Player,
    Game.World.Terrain,
    Engine.Renderer,
}
```

| Include Path | Resolved File |
|---|---|
| `Game.Player` | `Game/Player.hy` |
| `Game.World.Terrain` | `Game/World/Terrain.hy` |
| `Engine.Renderer` | `Engine/Renderer.hy` |

The source root defaults to the directory containing the file being compiled. Use the `--src-dir` flag to override it (see [Compiler Flags](#compiler-flags) below).

---

## Transitive Includes

If a module you include itself contains an `include` block, those declarations are automatically merged into the compilation unit. You do not need to manually include a module's dependencies — they are resolved transitively.

For example, suppose `Game/Player.hy` contains:

```hylian
include {
    std.io,
    std.errors,
}

public class Player {
    // ...
}
```

When your file includes `Game.Player`, the declarations from `std.io` and `std.errors` are also made available automatically. You do not need to re-include them:

```hylian
include {
    Game.Player,
}

Error? main() {
    Player p = new Player("Alice", 100);
    println(p.getName());   // println available via transitive include of std.io
    return nil;
}
```

---

## How It Works at the ASM Level

Each standard library module is pre-compiled into a `.o` object file stored under `runtime/std/`. When the Hylian compiler processes an `include { std.io, }` directive, it emits `extern` declarations in the generated `.asm` file for every symbol provided by that module. The symbols are resolved at link time by passing the corresponding `.o` files to `gcc`.

### Example

For a program that includes `std.io` and `std.system.filesystem`:

```hylian
include {
    std.io,
    std.system.filesystem,
}
```

The compiler emits something like the following at the top of the generated assembly:

```hylian
extern println
extern print
extern readln
extern fs_read
extern fs_write
extern fs_exists
```

You then link the required object files explicitly:

```sh
./hylian main.hy -o main.asm
nasm -f elf64 main.asm -o main.o
gcc main.o runtime/std/io.o runtime/std/filesystem.o -o main -no-pie
```

For user modules, the compiler compiles the referenced `.hy` files and includes their generated `.o` files in the same link step. The build tool `linkle` (coming soon) will handle all of this automatically.

---

## Compiler Flags

Two compiler flags are relevant to the module system:

### `--src-dir <dir>`

Sets the base directory used to resolve user module paths. By default this is the directory containing the source file being compiled.

```sh
./hylian src/main.hy --src-dir src/ -o build/main.asm
```

With `--src-dir src/`, an include of `Game.Player` resolves to `src/Game/Player.hy`.

### `--target <platform>`

Selects the target platform for code generation. This affects calling conventions, system call numbers, and object file format. Valid values are:

| Value | Platform |
|---|---|
| `linux` | Linux (ELF64, System V ABI) |
| `macos` | macOS (Mach-O 64-bit) |
| `windows` | Windows (PE32+, Microsoft x64 ABI) |

```sh
./hylian main.hy --target linux -o main.asm
./hylian main.hy --target macos -o main.asm
./hylian main.hy --target windows -o main.asm
```

The default target is `linux` if `--target` is not specified.