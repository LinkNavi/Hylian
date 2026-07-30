# Standard Library

`stdlib/` is a flat, syscall-based tree — not a `std.*`-namespaced set. Each page here
covers one module, addressed by the dotted include path shown in its title (see
[Modules and Includes](../language/modules.md) for how a dotted path like `os.fs`
resolves to `stdlib/os/fs.hy` on disk).

| Include path | Description |
|---|---|
| [`io`](io.md) | Console output/input |
| [`string`](string.md) | String manipulation |
| [`mem`](mem.md) | The arena allocator backing `new` — always linked |
| [`runtime`](runtime.md) | Builtins behind the `print`/`println` syntax — always linked |
| [`net`](net.md) | Raw IPv4/TCP sockets |
| [`time`](time.md) | Wall-clock time and sleep |
| [`os.exec`](os.exec.md) | Process creation and control |
| [`os.fs`](os.fs.md) | Filesystem operations |
| [`os.mount`](os.mount.md) | Mounting and power control |
| [`os.user`](os.user.md) | User/group identity |
| [`platform.linux_x86_64`](platform.linux_x86_64.md) | Syscall numbers, flag constants, `raw_alloc`/`raw_free` |

Nearly every function in this tree is an ordinary (non-`naked`) function, which means
calling one from your own (also non-`naked`) code and using the result any later than
the immediately following statement runs into
[the register-clobbering issue described first in Known Limitations](../language/known-limitations.md#a-function-calls-result-does-not-reliably-survive-a-later-call-in-the-same-function).
Using a return value right away works; storing it and using it after anything else
gets called — including returning it — currently doesn't. Every code sample on the
pages below is written the way the function is intended to be used; that intent isn't
the same as a guarantee it survives to the next call in today's backend.
