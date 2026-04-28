# Hylian — Current To-Do

## Language Features

### Critical for Build System
- [ ] Subprocess spawning with output capture — `std.process` (spawn, stdout/stderr capture, exit code)
- [ ] `std.path` — join, basename, dirname, extension
- [ ] Glob / directory listing in stdlib
- [ ] `std.strings` prefix rewriting (so `length()` works instead of `hylian_length()`)

### Core Language Gaps
- [ ] Enums
- [ ] `match` statement — keyword is lexed, no grammar rule or codegen
- [ ] Variable-length built-in method calls — `array.push(foo)`, `array.pop()`
- [ ] Casting between types
- [ ] Pointer/reference types beyond `&` in for-in (needed for mutation through function calls)
- [ ] Multi-return working end-to-end — parser has it, codegen skips tuples mostly
- [ ] Compile-time constants — `const` is lexed but unused
- [ ] Structs (optional)

---

## Compiler Optimizations

### IR Pipeline (prerequisite for all optimizations)
- [ ] Design IR — flat three-address instructions (opcode + up to 2 sources + 1 destination)
- [ ] Write AST → IR lowering pass
- [ ] Rewrite `codegen_asm.c` to consume IR instead of AST nodes

### Optimization Passes (on IR)
- [ ] Constant folding — `2 + 3` → `5` at compile time
- [ ] Constant propagation — replace uses of `x` with its value when `x` is never reassigned
- [ ] Dead code elimination — remove assignments whose result is never read
- [ ] Common subexpression elimination — compute `a + b` once if used multiple times
- [ ] Function inlining — substitute small function bodies at call sites

---

## LSP Gaps

- [ ] `match` — no hover or completion understanding
- [ ] Enum types won't resolve until enums are added to the language
- [ ] `print` / `println` not fully typed for hover (show as unknown return)
- [ ] No go-to-definition — only hover and completion exist
