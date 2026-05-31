# Summary

[Introduction](README.md)

# Language Reference

- [Syntax](language/syntax.md)
  - Types, variables, functions, classes, enums, switch, pointers, tuples, unsafe, const, static arrays, bitwise ops, struct literals, `as` casts, `null`, digit separators, `union class`, and more
- [Error Handling](language/error-handling.md)
- [Modules & Standard Library](language/modules.md)
  - Module visibility: `public` functions, statics, consts, and enums
- [Vendor Packages & FFI](language/vendors.md)
  - Wrapping C libraries, `.hyi` interface files, auto-generating bindings with `bindgen.py`, filtered bindgen for large SDKs, and native-feeling FFI patterns
- [Kernel Development Guide](language/kernel.md)
  - [x86-64 Intrinsics Quick Reference](intrinsics-quick-reference.md)
  - [Intrinsics Implementation Details](intrinsics-implementation.md)
  - [Before/After Comparison Examples](examples/intrinsics_comparison.md)

# Standard Library

- [std.io](stdlib/std.io.md)
- [std.errors](stdlib/std.errors.md)
- [std.strings](stdlib/std.strings.md)
- [std.system.env](stdlib/std.system.env.md)
- [std.system.filesystem](stdlib/std.system.filesystem.md)