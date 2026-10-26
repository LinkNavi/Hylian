# Known Limitations

This page lists the gaps that remain in the language and toolchain, each confirmed by
hand against the current compiler with a real `./hylian ... -o out.o` + `gcc` + run
cycle. If something here has since been fixed, trust the compiler over this page.

Earlier versions of this page described a much longer list — register clobbering across
calls, `i++` doing nothing, `for` post-clauses, class fields, `Err`/`panic`, arrays,
`EnumName.Variant`, printing computed strings, string interpolation. All of those work
now; the regression tests for them live in `tests/stdlib/language.hy`.

## Language

### Nested generics, other than `array<array<T>>`, need a space

`array<array<int>>` and `cast<array<T>>(x)` parse, but the lexer reads `>>` as a shift
operator everywhere else. Write `multi<int | array<int> >` with a space.

### A bare ternary *statement* is read as a nullable declaration

`Foo ? x : y;` at the start of a statement parses as the start of a `Foo? x` declaration
(bison reports this as 2 reduce/reduce conflicts and picks the declaration). A ternary is
fine anywhere inside an expression, which is the only place it's useful.

### Class values are stored inline

A `class` local holds its object directly rather than a pointer to a heap block, so
`cast<MyClass>(raw_alloc(n))` does not give you a heap object that survives being returned
from a function. `new` objects are fine; the stdlib modules that need a heap-allocated
handle (`map`, `bytes` buffers, `http` responses) use an opaque `usize` pointing at a raw
word block instead.

### A range loop's end is re-evaluated each iteration

`for (i in 0..n)` is a plain `for (int i = 0; i < n; i += 1)`, so changing `n` inside the
body changes the loop bound.

### Default arguments are filled in at the call site

`int f(int a, int b = 5)` inserts the default expression into each call that omits it, in
the *caller's* scope — use literals and constants. Not supported for methods.

### Fixed-size local arrays are word arrays

`int buf[16];` is a zero-filled `array<int>` of length 16; every element is one 8-byte word
regardless of the declared element type (so `uint8 b[64]` uses 512 bytes). For a packed byte
buffer use `static uint8 buf[64];` or `bytes_alloc`.

### Keywords added

`do`, `defer`, `typedef` and `sizeof` are now reserved words (as are `in`, `size_of`,
`cast`, `as`, ...). A variable named `in`, `do`, `defer` or `typedef` won't parse.

### Interpolation quirks

Quotes inside `{{ }}` must be escaped (`"{{ok ? \"y\" : \"n\"}}"`), and a string that uses
interpolation can't also contain a literal `{{`.

### No multi-return / tuples

Functions return exactly one value.

### Function pointers are untyped

`usize fp = addrof_fn(f); fp(1, 2)` calls through the address, and the address can be
handed to C as a callback. The pointer carries no signature, so nothing checks the
arguments, and the result of an indirect call is always integer-sized (a function
returning `float` through a pointer will not come back in the right register).

## Floating point

- Calls follow the SysV x86-64 ABI, so `float` / `double` / `float32` arguments and results
  travel in `xmm` registers and mix freely with C. Flat structs pass and return **by value**
  to C (raylib's `Vector2` / `Color`, SDL's `SDL_Rect`). Not handled: `long double`, and
  structs containing nested structs, arrays, unions or padding.
- Only scalar arithmetic and comparisons exist, and `math.hy`'s transcendental functions are
  ~1e-12 accurate, not libm-exact (call libm through an FFI declaration when you need it).
- `array<float>` stores each element as a raw 8-byte word; indexing, `pop()` and `for (e in arr)` reinterpret it as a float.
- Optimisation runs on typed MIR in the hygen middle end (`hygen/libhyopt`), not on the
  untyped IR: integer division/modulo by constants, comparisons and shifts are folded with the
  operand's real signedness and width, and `x / 0.0` folds to IEEE `inf`/`nan`. Integer division
  by a *zero* constant is left to the hardware. The old IR-level passes (`compiler/opt.c`) are
  disabled by default (`HYLIAN_IROPT=1` re-enables them); only dead-function pruning still runs there.

## Toolchain

- **Static library code must be re-installed after edits.** Zora compiles stdlib modules
  from `$HYLIAN_LIB` or `/usr/local/lib/hylian/std`; run `./devInstall.sh` after changing
  `stdlib/`, and delete a project's `zora-build/.hylian-cache` if a stale object is linked.
- **The kernel syscall round trip** (`syscall` -> LSTAR handler -> `sysret()` executed from
  ring 0) is not verified; a real one needs a ring-3 jump and TSS. The MSR/builtin setup
  itself is verified.
- `tree-sitter-hylian/` (the packaged copy under `lsp/`) is not kept in step with
  `lsp/grammar/grammar.js`; `tests/grammar_drift.sh` checks the compiler, the LSP and
  `lsp/grammar`.
