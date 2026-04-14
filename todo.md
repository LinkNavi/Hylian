# Hylian Language TODO

## Current Status
✅ Lexer and parser working with Flex/Bison
✅ Full AST construction for classes, methods, fields, statements, expressions
✅ C++ code generation producing compilable output
✅ Method parameters support
✅ Basic operators: <, >, <=, ==, !=, +, -
✅ Control flow: if/else, while
✅ Types: int, str, Error, void, custom classes
✅ Nullable types (Error?)
✅ Test suite (test.sh) - all tests passing

## High Priority (Core Language Features)

### 1. Constructors
- [ ] Parse constructor syntax in classes
- [ ] Generate C++ constructors
- [ ] Handle `new ClassName(args)` properly in codegen
- [ ] Support constructor parameters
- [ ] Default constructors for classes without explicit ones

### 2. Function Calls with Arguments
- [ ] Parse function/method call arguments in expressions
- [ ] Store arguments in MethodCallNode AST
- [ ] Generate C++ method calls with arguments
- [ ] Test: `player.setHealth(100)`, `obj.method(a, b, c)`

### 3. Top-Level Functions
- [ ] Add function_decl to program grammar (already partially there)
- [ ] Generate top-level C++ functions
- [ ] Support `main()` function as entry point
- [ ] Test: `Error? main() { ... }`

### 4. For Loops
- [ ] C-style for loop codegen: `for (int i = 0; i < 10; i++)`
- [ ] Range-based for loop codegen: `for (item in items)`
- [ ] Need array/collection types first for range-based

### 5. More Operators
- [ ] Arithmetic: *, /, %
- [ ] Logical: &&, ||, !
- [ ] Increment/decrement: ++, --
- [ ] Assignment operators: +=, -=, *=, /=
- [ ] Operator precedence handling

### 6. Arrays/Collections
- [ ] Array syntax: `int[] items`
- [ ] Array literals: `{1, 2, 3}`
- [ ] Array indexing: `items[0]`
- [ ] Dynamic arrays (map to std::vector)
- [ ] Slices (future feature)

## Medium Priority (Language Features)

### 7. String Interpolation
- [ ] Parse `$"text {expr}"` syntax
- [ ] Generate C++ string concatenation or std::format
- [ ] Handle nested expressions

### 8. Error Handling Improvements
- [ ] Implement `Err("message")` function in runtime
- [ ] Better nil handling
- [ ] Error propagation patterns
- [ ] Go-style error checking codegen

### 9. Defer Statement
- [ ] Parse defer syntax (already in lexer)
- [ ] Generate C++ RAII wrapper or scope guards
- [ ] Test cleanup patterns

### 10. Properties (C#-style)
- [ ] Parse get/set syntax
- [ ] Generate C++ getter/setter methods
- [ ] Automatic backing fields

### 11. Interfaces
- [ ] Parse interface declarations
- [ ] Generate C++ abstract base classes
- [ ] Interface implementation checking

### 12. Generics
- [ ] Parse generic type parameters: `class List<T>`
- [ ] Template expansion in codegen
- [ ] Generate C++ templates

### 13. Namespaces/Modules
- [ ] Improve include system
- [ ] Map to C++ namespaces
- [ ] Module resolution

## Runtime/Standard Library

### 14. Core Runtime
- [ ] Reference counting implementation
- [ ] Memory management helpers
- [ ] Error type improvements (stack traces?)
- [ ] Panic/assert functions

### 15. Standard Library - Core
- [ ] String type with methods (length, substr, contains, split, etc.)
- [ ] Array/List type
- [ ] HashMap/Dictionary type
- [ ] Optional type
- [ ] Result type

### 16. Standard Library - I/O
- [ ] print, println functions
- [ ] File I/O (open, read, write, close)
- [ ] Standard input/output
- [ ] Error handling for I/O

### 17. Standard Library - Utilities
- [ ] Math functions
- [ ] String formatting
- [ ] JSON parsing/serialization
- [ ] HTTP client (for build system example)

## Build System (End Goal)

### 18. Self-Hosting Build System
**Goal:** Create a Make/Ninja replacement written in Hylian that can build Hylian projects

Features needed:
- [ ] File system operations (stat, glob, directory walking)
- [ ] Process spawning (execute shell commands)
- [ ] Dependency graph construction
- [ ] Incremental build tracking (mtime or hash-based)
- [ ] Parallel execution
- [ ] Build configuration DSL

Example syntax:
```hylian
include {
    build.fs,
    build.process,
    build.graph
}

src_main = glob("src/*.hy")
inc_main = glob("include/*.hy")

target executable(
    name: "myapp",
    sources: src_main,
    includes: inc_main,
    libs: ["pthread"],
    flags: ["-O2"]
)

target clean() {
    exec("rm -rf build")
}
```

Tasks:
- [ ] Design build.hy syntax
- [ ] Implement file system stdlib
- [ ] Implement process execution stdlib
- [ ] Write dependency resolver
- [ ] Implement parallel task execution
- [ ] Add colored output (Cargo-style)
- [ ] Incremental build cache
- [ ] Cross-platform support (Linux/Windows/macOS)
- [ ] Self-host: Use Hylian build system to compile Hylian compiler

## Quality of Life

### 19. Better Error Messages
- [ ] Line/column numbers in parser errors
- [ ] Syntax error recovery
- [ ] Type checking errors
- [ ] Helpful suggestions

### 20. Tooling
- [ ] Syntax highlighting (vim, vscode)
- [ ] Language server protocol (LSP)
- [ ] Formatter
- [ ] Linter

### 21. Documentation
- [ ] Language specification
- [ ] Standard library documentation
- [ ] Tutorial/getting started guide
- [ ] Example projects

## Testing & Validation

### 22. Compiler Tests
- [ ] Expand test.sh with more edge cases
- [ ] Negative tests (should fail to compile)
- [ ] Runtime tests (execute generated code)
- [ ] Performance benchmarks

### 23. Real Projects
- [ ] Build the build system in Hylian
- [ ] Port a small utility (CLI tool)
- [ ] Build a web server
- [ ] Build Slate (text editor) in Hylian?

## Future/Research

- [ ] LLVM backend (instead of transpiling to C++)
- [ ] Compile to C (not C++) for kernel modules
- [ ] JIT compilation
- [ ] Debugger integration
- [ ] Package manager
- [ ] Cross-compilation support
- [ ] WebAssembly target

## Notes

**Philosophy:**
- Speed over safety (no borrow checker)
- Let users shoot themselves in the foot
- C#-style ergonomics
- Go-style error handling
- Transpile to C++ for now (mature toolchain, optimizations)

**Current Limitations:**
- No type checking yet (relies on C++ compiler)
- No semantic analysis
- Limited standard library
- Manual memory management (refcounting planned)
