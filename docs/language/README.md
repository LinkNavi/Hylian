# Language Reference

This section documents the Hylian language itself, grounded directly in
`compiler/parser.y`, `compiler/lexer.l`, `compiler/ast.h`, and the rest of the
compiler's typechecking/lowering passes.

- **[Syntax](syntax.md)** — the full grammar: types, variables, operators, functions,
  control flow, classes, enums, arrays/`multi`, pointers, `unsafe`/`naked`/inline
  assembly, `@target` conditional compilation, and includes.
- **[Error Handling](error-handling.md)** — `Error?`, `Err(...)`, `panic(...)`.
- **[Modules and Includes](modules.md)** — the `include` system, path resolution, the
  actual current standard library module list, and `module`/`public` visibility.
- **[Known Limitations](known-limitations.md)** — a verified list of what parses and
  typechecks but doesn't yet compile-and-link cleanly, so the rest of this reference
  can describe the language's design without re-litigating current backend gaps on
  every page.

For the standard library's function-level reference, see
[the stdlib docs](../stdlib/README.md). For the build tool, vendor/FFI system, and freestanding
kernel-mode development, see [Vendor Packages & FFI](vendors.md) and the
[Kernel Development Guide](kernel.md) — those pages cover tooling and platform-specific
workflows on top of the language described here.
