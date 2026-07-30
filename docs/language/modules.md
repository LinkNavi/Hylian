# Modules and Includes

`include` is the only mechanism for pulling in code from elsewhere — the standard
library, other files in your own project, or vendor packages. There's no separate
`import`/`use` keyword.

## Include Syntax

```hylian
include {
    io,
    string,
}
```

A trailing comma on the last entry is allowed (and conventional, for clean diffs). The
block, if present, should come before other declarations, though the parser's `program`
rule actually allows `include` and `ccpinclude` statements interleaved with
declarations anywhere at the top level — putting them first is a convention, not a
requirement enforced by the grammar.

## Path Resolution

Each entry is a dotted path. `compiler/compiler.c`'s `module_to_filepath` turns every
`.` in the path into a `/` and appends `.hy`:

```
io          -> io.hy
os.fs       -> os/fs.hy
platform.linux_x86_64 -> platform/linux_x86_64.hy
```

The resulting relative path is first looked up under the compiler's `--src-dir`; if
that doesn't exist, it falls back to a path relative to the *including file's own*
directory. Included files are compiled and merged in-place into the including
program — this is a textual/AST merge (similar in effect to a C `#include`, not a
namespaced module import), so includes are transitively flattened: if `io.hy`
includes `string.hy`, a file that includes `io` gets `string`'s declarations too.

## The Standard Library

`stdlib/` is organized as plain files and directories, addressed by the same dotted
path convention described above. This is the actual, current module surface — not the
`std.*`-namespaced set from older drafts of this documentation, which doesn't match
what's on disk:

| Include path | File | Contents |
|---|---|---|
| `io` | `stdlib/io.hy` | `print`, `println`, `eprint`, `eprintln`, `int_to_str`, `str_to_int`, `read_line` |
| `string` | `stdlib/string.hy` | `length`, `is_empty`, `equals`, `index_of`, `contains`, `starts_with`, `ends_with`, `slice`, `trim`/`trim_start`/`trim_end`, `to_upper`, `to_lower`, `concat` |
| `mem` | `stdlib/mem.hy` | The arena allocator backing `new` (`arena_init`/`arena_alloc`/`arena_free`) — always linked, not something you `include` directly. |
| `runtime` | `stdlib/runtime.hy` | `hylian_print`/`hylian_println`, the builtins the `print`/`println` *syntax* itself compiles down to — always linked regardless of what you `include`. |
| `net` | `stdlib/net.hy` | Raw IPv4/TCP sockets: `tcp_socket`, `connect`, `bind`, `listen`, `accept`, `send`, `recv`, `close`, `shutdown` |
| `time` | `stdlib/time.hy` | `now`, `nanos`, `millis`, `sleep` |
| `os.exec` | `stdlib/os/exec.hy` | `fork`, `pid`, `kill`, `wait`, `exec`, `spawn` |
| `os.fs` | `stdlib/os/fs.hy` | `open`, `create`, `close`, `read`, `write`, `pread`, `pwrite`, `read_all`, `write_all`, `exists`, `mkdir`, `rmdir`, `remove`, `chmod`, `chown`, `cwd`, `chdir`, `dup`, `dup2`, `pipe` |
| `os.mount` | `stdlib/os/mount.hy` | `mount`, `unmount`, `reboot`, `poweroff`, `halt`, `enable_cad`, `disable_cad` |
| `os.user` | `stdlib/os/user.hy` | `uid`, `gid`, `euid`, `egid`, `set_uid`, `set_gid`, `is_root` |
| `platform.linux_x86_64` | `stdlib/platform/linux_x86_64.hy` | Syscall numbers, flag constants, and the shared `raw_alloc`/`raw_free` every other module builds on |

Full per-function signatures and examples live under [`stdlib/`](../stdlib/README.md).

`mem` and `runtime` are **always** linked into every compiled program regardless of
what you `include` — the compiler unconditionally emits calls to `arena_init`/
`arena_alloc`/`arena_free` for `new`, and to `hylian_print`/`hylian_println` for the
`print`/`println` *syntax*, whether or not the corresponding module is included.

Most higher-level modules `include { platform.linux_x86_64, ... }` themselves to reach
`raw_alloc`/`raw_free` and the syscall-number constants — you generally don't need to
include `platform.linux_x86_64` yourself unless you're calling `syscall(...)` directly
in your own code.

There is currently no `std.errors`, `std.crypto`, or `std.networking.*` in `stdlib/` —
those names appear in some older tooling configuration but don't correspond to files
that exist today. `Err`/`panic` are compiler builtins, not a stdlib module (see
[Error Handling](error-handling.md)).

## Module Visibility

A `module Name { ... }` block groups declarations under a namespace and controls what
callers outside the module can see, via `public`.

### Public Functions

```hylian
module MathUtils {
    int square(int x) {          // private — only visible inside this module
        return x * x;
    }

    public int cube(int x) {     // visible to any file that includes this module
        return x * square(x);
    }
}
```

```hylian
include { MathUtils, }

int main() {
    int result = MathUtils.cube(3);   // 27
    return 0;
}
```

### Public Static Variables

```hylian
module Config {
    public static int max_connections = 100;
    static int internal_counter = 0;    // not visible outside
}
```

### Public Constants

```hylian
module Protocol {
    public const int VERSION = 3;
    const int INTERNAL_FLAG = 0xFF;    // not visible outside
}
```

### Public Enums

An enum declared inside a `module` must itself be `public` to be reachable from
outside:

```hylian
module Status {
    public enum Code {
        OK       = 0,
        NotFound = 1,
    }
}
```

(As with top-level enums, member access like `Status.Code.OK` currently doesn't link
— see [Known Limitations](known-limitations.md#enumnamevariant-doesnt-link).)
