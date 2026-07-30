# Summary

[Introduction](README.md)

# Language Reference

- [Overview](language/README.md)
- [Syntax](language/syntax.md)
  - Types, variables, operators, functions, control flow, classes, enums,
    arrays/`multi`, pointers, casting, `unsafe`/`naked`/inline assembly, `@target`
    conditional compilation, and includes
- [Error Handling](language/error-handling.md)
- [Modules & Standard Library](language/modules.md)
  - The `include` system, path resolution, and `module`/`public` visibility
- [Known Limitations](language/known-limitations.md)
  - What currently parses and typechecks but doesn't yet compile-and-link cleanly
- [Vendor Packages & FFI](language/vendors.md)
  - Wrapping C libraries, `.hyi` interface files, and native-feeling FFI patterns
- [Kernel Development Guide](language/kernel.md)
  - Freestanding/kernel mode, bare-metal examples
- [Termina Backend](language/termina-backend.md)
- [Intrinsics: Before/After Comparison](examples/intrinsics_comparison.md)

# Standard Library

- [Overview](stdlib/README.md)
- [io](stdlib/io.md) — console output/input
- [string](stdlib/string.md) — string manipulation
- [mem](stdlib/mem.md) — the arena allocator backing `new` (always linked)
- [runtime](stdlib/runtime.md) — builtins behind the `print`/`println` syntax (always linked)
- [net](stdlib/net.md) — raw IPv4/TCP sockets
- [time](stdlib/time.md) — wall-clock time and sleep
- [os.exec](stdlib/os.exec.md) — process creation and control
- [os.fs](stdlib/os.fs.md) — filesystem operations
- [os.mount](stdlib/os.mount.md) — mounting and power control
- [os.user](stdlib/os.user.md) — user/group identity
- [platform.linux_x86_64](stdlib/platform.linux_x86_64.md) — syscall numbers, flags, `raw_alloc`/`raw_free`
