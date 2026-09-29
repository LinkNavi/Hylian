# Syntax Reference

This page documents the Hylian language grammar as implemented in
`compiler/parser.y`, `compiler/lexer.l`, and `compiler/ast.h` — not an idealized design.
Where the parser accepts something the current backend can't yet execute, there's a
pointer to [Known Limitations](known-limitations.md) rather than a silent omission.

## File Structure

A `.hy` file is a flat sequence of top-level items: an optional `include { ... }`
block, any number of `ccpinclude` lines, and any number of `class`, `enum`, `module`,
function, `typedef`, or `static`/`const`/plain global variable declarations, in any order. There's no required
entry point name at the parser level, but the toolchain's convention is a function
named `main`.

```hylian
include {
    io,
    string,
}

const int MAX_RETRIES = 3;

enum Status { Ok, Failed }

int main() {
    return 0;
}
```

## Comments

`//` runs to end of line, and `/* ... */` comments may span lines (they do not nest).

## String and Character Literals

A string literal is `"..."` with C-style escapes decoded by the lexer: `\n \t \r \0 \a \b \f \v \e`
(ESC, for ANSI colours), `\\ \' \"`, and `\xNN` (one or two hex digits). An unrecognised escape
is left as-is, backslash included, so typos stay visible.

A character literal is `'a'`, `'\n'`, `'\x41'`, `'\''` — it is an **`int`** holding the
byte value (`'a'` is `97`), usable anywhere an integer is:

```hylian
int c = 'a';
if (s[0] == 'J') { ... }
```

## Strings

`str` values are pointers to NUL-terminated bytes. The language has a few operations
built in, with no `include` needed:

```hylian
str a = "foo" + "bar";     // concatenation (both sides str)
a += "!";                  // append (makes a new string; strings are immutable)
int b = a[1];              // byte at index 1 (0-255), unchecked
buf[0] = 'J';              // store a byte into a writable buffer
if (a == "foo!") { ... }   // content comparison (needs `include { string }`)
```

Strings built at runtime come from a never-freed pool, so they are safe to return from
functions.

## String Interpolation

`{{expr}}` inside a string literal evaluates any expression and splices its text in:

```hylian
int n = 5;
str who = "world";
println("n={{n}}, next={{n + 1}}, hello {{who}}!");
println("{{ok ? \"yes\" : \"no\"}}");        // quotes inside {{ }} are escaped
```

Each expression is converted by its type — `str` as-is, `bool` as `true`/`false`,
floats with up to six fractional digits, everything else as a decimal integer — and the
pieces are concatenated. The lexer rewrites the literal into an internal
`__interp(...)` call, so the expression is parsed by the normal expression grammar.
A literal `{{` can't be written inside a string that also needs interpolation.

## Numeric Literals

- Decimal integers: `42`, with optional `_` digit separators (`1_000_000`).
- Hexadecimal: `0xFF`; binary: `0b1010_0101`; octal: `0o755` — all allow `_` separators.
- Floats: `3.14`, `1_000.5`, and exponents: `1.5e3`, `2.0e-2`, `1e10`. (No bare `.5` / `5.`.)
- Booleans: `true`, `false`.
- Null: both `nil` and `null` are accepted and produce the same literal.

## Types

| Category | Syntax | Notes |
|---|---|---|
| Integer | `int` | Signed, general-purpose integer. |
| Sized integers | `int8`, `int16`, `int32`, `int64`, `uint8`, `uint16`, `uint32`, `uint64` | Arithmetic **wraps at the type's width** like C (`uint8 c = 250; c = c + 10;` gives `4`), unsigned types divide, shift and compare as unsigned, and stores convert to the variable's declared type. Spelled as ordinary identifiers (the lexer has no dedicated tokens), so they parse via the generic `IDENTIFIER` type rule. |
| String | `str` | A pointer to a nul-terminated byte buffer; `str` values are raw pointers under the hood. |
| Boolean | `bool` | `true` / `false`. |
| Void | `void` | Function/method return type only. |
| Error | `Error` | See [Error Handling](error-handling.md). |
| Float | `float` (= `double` = `float64`), `float32` | Scalar floating point (SSE2): `+ - * / < > ==`, unary `-`, mixed int/float arithmetic (`f * 2` converts the int), `cast<int>(f)` / `cast<float>(i)`, float globals, `float` parameters and return values, and `println(f)` / interpolation. `float32` widens to `float` when mixed with one. Floats follow the SysV ABI (xmm registers), so they can be passed to and returned from C functions and Hylian functions can be called from C. |
| C-style aliases | `char` `byte` `u8` `u16` `u32` `u64` `i8` `i16` `i32` `i64` `short` `ushort` `uint` `long` `ulong` `size_t` `ssize_t` | Built-in spellings of the sized types above (`char` = `uint8`, `uint` = `uint64`, `long` = `int64`, ...). |
| User alias | `typedef T Name;` | Top-level only. `typedef uint8 octet;` then `octet o = 7;` — resolved at parse time. |
| Pointer-sized | `usize`, `isize` | Unsigned/signed pointer-sized integers, used throughout the stdlib for raw addresses. |
| Auto | *(no keyword)* | Only reachable via `name := expr;` — there is no explicit `auto` type you can write in a signature. |
| Class / enum name | any `IDENTIFIER` | A user-defined class or enum used as a type. |
| Reference | `&T` | `TYPE_REF` — e.g. `&int`. |
| Raw pointer | `*T` | `TYPE_RAWPTR` — e.g. `*uint8`. |
| Nullable | `T?` | A `?` suffix after any base type marks it nullable (allows `nil`). Valid on `var_decl` and on function/method return types (`Error? main()`). |
| Fixed array | `array<T, N>` | Heap-backed, fixed capacity `N`. |
| Growable array | `array<T>` | Heap-backed, flexible capacity. Nested generics close with `>>`: `array<array<int>>`. |
| Tagged union | `multi<A \| B \| ...>` | Optionally `multi<A \| B \| ..., N>` for a fixed backing size. |
| `any` union | `multi<any>` | Optionally `multi<any, N>`. |

Arrays and `multi` are described further in [Arrays and Tagged Unions](#arrays-and-tagged-unions-multi) below.

## Variables

```hylian
int x = 42;
str name;              // declared, no initializer
int? maybe = nil;      // nullable
str? other;            // nullable, no initializer
x := 10;                // `:=` — type inferred as `auto`, must have an initializer
```

`:=` only works with a bare identifier on the left (no type annotation) and always
requires an initializer.

A **fixed-size local array** is declared `T name[N];` (N any expression). It is a
zero-filled `array<T>` whose length is already N, so `.len`, indexing and `for`-`in` all
work:

```hylian
int buf[16];
buf[0] = 1;
buf[3] += 4;
int n = buf.len;        // 16
```

At the top level a plain `int counter = 0;` is a global variable (the same as
`static int counter = 0;`).

### `static` and `const`

`static` declares a mutable global. It's valid at the top level of a file, inside a
`module` block, and inside a function body (a function-local static, still a single
global storage slot, not re-initialized on every call):

```hylian
static int counter = 0;
static int counter2;            // no initializer
static int buffer[256];         // fixed-size static array — no initializer allowed here
public static int shared = 1;   // visible outside the file/module that defines it
```

`const` is the same shape but only supports the with-initializer form, and has no
fixed-size-array variant:

```hylian
const int MAX = 100;
public const str VERSION = "0.1.0";
```

## Operators and Precedence

From lowest to highest precedence, exactly as declared in `parser.y`:

| Level (low → high) | Operators | Associativity |
|---|---|---|
| 1 | `=` `+=` `-=` `*=` `/=` `%=` `<<=` `>>=` `&=` `\|=` `^=` | right |
| 1.5 | `? :` (ternary) | right |
| 2 | `\|\|` | left |
| 3 | `&&` | left |
| 4 | `\|` (bitwise or) | left |
| 5 | `^` (bitwise xor) | left |
| 6 | `&` (bitwise and) | left |
| 7 | `==` `!=` | left |
| 8 | `<` `>` `<=` `>=` | left |
| 9 | `<<` `>>` | left |
| 10 | `+` `-` | left |
| 11 | `*` `/` `%` | left |
| 12 | unary `!` `~` unary `-` | right |
| 13 | `as` | left |
| 14 | postfix `++` `--` | left |
| 15 | `.` | left |
| 16 | `[` `]` | left |

The one detail worth calling out explicitly: bitwise `&`, `^`, and `\|` bind **looser**
than `==`/`!=` and the relational operators — the same famously-surprising rule C has.
`a & b == c` parses as `a & (b == c)`, not `(a & b) == c`. Parenthesize bitwise
expressions mixed with comparisons.

**Ternary:** `cond ? a : b` evaluates only the chosen arm and nests to the right
(`a ? x : b ? y : z`). As a bare *statement* the parser reads `Foo ? x` as a nullable
declaration, so use it inside an expression (`int m = a > b ? a : b;`).

**Compound assignment** (`+= -= *= /= %= <<= >>= &= |= ^=`) works on a variable, a field,
an element, and a dereference: `x += 1; p.x *= 3; a[i] <<= 2; *ptr |= 4;`. The field /
element / pointer forms are rewritten to `target = target op value`, so keep side effects
out of the target expression (`a[f()] += 1` would call `f` twice). `:=` only targets a
bare identifier.

Increment/decrement (`++x`, `x++`, `--x`, `x--`) work in prefix and postfix, in
expression and statement position, and as a `for` post-clause.

## Functions

```hylian
int add(int a, int b) {
    return a + b;
}

// Nullable return type
Error? save(str path) {
    return nil;
}

// naked: no implicit arena setup/teardown — see stdlib/mem.hy. Still gets an
// ordinary push-rbp/leave-ret stack frame; "naked" only opts out of the arena calls.
naked usize raw_alloc(int size) {
    return cast<usize>(syscall(9, 0, size, 3, 34, -1, 0));
}
```

A top-level function prefixed with `static` is private to its file (this reuses the
`static` keyword, distinct from a `static` *variable* declaration — the grammar
disambiguates by what follows). Every ordinary (non-`naked`) function implicitly gets
a hidden arena slot threaded through by the compiler for `new` allocations, via calls
to `arena_init` at entry and `arena_free` right before every `return`; `naked`
functions opt out of that entirely (see `stdlib/mem.hy`'s comment block for why the
arena allocator itself has to be `naked`).

That `arena_free` call has a real, verified consequence:
**[calling another function and using its result any later than the immediately following statement is unreliable in a non-`naked` function](known-limitations.md#a-function-calls-result-does-not-reliably-survive-a-later-call-in-the-same-function)**
— including simply returning it — because the cleanup call clobbers the scratch
register the value was sitting in. This is the most impactful item in
[Known Limitations](known-limitations.md); read it before writing anything beyond a
toy example.

Parameters are a comma-separated `type IDENTIFIER` list, optionally with defaults on the
trailing ones; there are no variadic parameters.

```hylian
int add(int a, int b = 5, int c = 100) { return a + b + c; }
str greet(str who, str greeting = "hello") { return greeting + " " + who; }

add(1);         // 106
add(1, 2);      // 103
greet("bob");   // "hello bob"
```

A default is filled in at the call site, so it should be a literal or constant (it is
evaluated in the *caller's* scope). Defaults work for plain functions, not methods.

## Control Flow

### `if` / `else if` / `else`

```hylian
if (x > 0) {
    ...
} else if (x < 0) {
    ...
} else {
    ...
}
```

### `while`

```hylian
while (i < 10) {
    i = i + 1;
}
```

### `for`

C-style three-clause, a range form, and a `for`-`in` over arrays.

```hylian
for (int i = 0; i < 5; i++) { ... }
for (int j = 10; j > 0; j -= 3) { ... }

for (i in 0..5) { ... }       // i = 0, 1, 2, 3, 4  (end exclusive)
for (i in 1..=3) { ... }      // i = 1, 2, 3        (end inclusive)

for (n in nums) { ... }       // by value
for (&n in nums) {            // by reference - n is effectively a pointer to the element
    *n = *n * 2;
}
```

The init- and post-clauses accept a declaration or assignment, `i++` / `i--` / `++i` /
`--i`, and compound assignment (`i += 2`); either may be omitted. The condition has no
"empty" form, so write `true` for an infinite loop: `for (; true ;) { ... }`.
A range loop's end expression is re-evaluated each iteration (it's a plain `for` underneath).

### `do` / `while`

```hylian
do {
    n += 1;
} while (n < 10);
```

The body runs at least once. `continue` jumps to the condition test and `break` leaves
the loop, as in C.

### `switch`

```hylian
switch (x) {
    case 1: {
        ...
    }
    case 2: {
        ...
    }
    default: {
        ...
    }
}
```

Every arm's body is its own brace-delimited block (no fallthrough between arms, and no
bare statement list without braces). `case` values are constant expressions; `default`
is optional and may appear anywhere among the arms.

### `break` / `continue`

Standard loop control, valid inside `while`/`for`/`for`-`in` bodies.

### `defer`

`defer` schedules code to run when the enclosing function returns — the tidy way to pair
an acquire with its release:

```hylian
int process(str path) {
    int fd = open(path, O_RDONLY, 0);
    if (fd < 0) { return -1; }
    defer close(fd);                    // runs on every way out of the function
    defer { println("done with {{path}}"); }

    if (bad(fd)) { return -2; }         // close(fd) still happens
    return 0;
}
```

- The operand is an expression (usually a call) or a `{ ... }` block of statements.
- Deferred code runs in **reverse** order of the `defer` statements, after a `return`'s value
  has been computed and before the function's locals go away, so it can use them.
- A `defer` only runs if control actually passed through it (one in an untaken `if` branch
  is skipped), and one inside a loop runs once, at function exit — not once per iteration.
- It does not run if the process exits or `panic`s.

## Classes

```hylian
public class Player {
    public str name;
    private int health;

    Player(str n, int h) {
        name = n;
        health = h;
    }

    int get_health() {
        return health;
    }

    naked void low_level_helper() { ... }
}
```

- A class is `public` (visible outside the file) or, with no modifier, private to the
  file that declares it.
- Fields default to **private** when written with no modifier (`type name;`);
  `public type name;` and `private type name;` are both explicit spellings of the
  public/private cases respectively.
- A constructor is written as a method whose body sits inside the class — by
  convention, and in every real example in this codebase, named the same as the
  class. (The grammar itself doesn't check that the name matches the class; it just
  treats the first constructor-shaped member — any `IDENTIFIER(params) { body }` not
  otherwise typed — as *the* constructor. Only one constructor is supported.)
- Methods may be `naked` and may have a nullable (`Type?`) return, same as free
  functions.
- **Class fields — both the implicit `name = n;` self-write shown above and explicit
  `obj.field` access — do not work end-to-end yet.** See
  [Known Limitations](known-limitations.md) before relying on this pattern; only
  zero-field classes reliably instantiate and link today.

### Modifiers: `packed` and `union`

```hylian
packed class TightlyPacked {
    public uint8 a;
    public uint32 b;
}

union class Reg {
    public uint64 qword;
    public uint32 dword;
}
```

`packed` removes alignment padding between fields. `union class` gives every field
offset 0 (C-union semantics — the class's size is its largest field's size); a union
class may declare fields but not methods or a constructor (the grammar's union
variants only collect `NODE_FIELD` members).

### Instantiation and struct literals

```hylian
Player p = new Player("Bob", 100);

// Struct literal — a stack-allocated value built directly from field: value pairs,
// bypassing any constructor
Point origin = Point { x: 0, y: 0 };
```

## Enums

```hylian
enum Status { Ok, Failed }

enum Flags {
    Read  = 1,
    Write = 2,
}
```

A variant with no explicit value gets **one more than the previous variant** (the first is
`0`), exactly as in C:

```hylian
enum E { A, B = 10, C }
// A = 0, B = 10 (explicit), C = 11
```

Enums are `public` or private-to-file, same rule as classes. Variants are plain
integers: `Status.Ok` works in expressions, interpolation and `switch` cases.

## Arrays and Tagged Unions (`multi`)

```hylian
array<int> nums = [1, 2, 3];
int first = nums[0];
nums[0] = 99;
int n = nums.len;      // element count
int c = nums.cap;      // backing capacity

multi<int | str> u = 42;
int tag = u.tag;       // which alternative is active (0-based)
```

Array literals, indexing (read via `expr[expr]`, write via `expr[expr] = expr;`),
`.len`/`.cap`/`.push()`/`.pop()`, and `multi`'s `.tag`/`.value` all parse and
typecheck. They currently fail to **link** — the runtime helper functions they lower
to don't exist yet anywhere in the codebase. See
[Known Limitations](known-limitations.md).

## Pointers, References, Casting, and Memory Safety

```hylian
int x = 5;
&int ref = &x;              // reference
*int ptr = cast<*int>(&x);  // raw pointer, explicitly cast

int y = *ptr;                // dereference (read)
*ptr = 10;                   // dereference (write)

unsafe {
    *ptr = 0xFF;
}

volatile *ptr = 0xFF;        // volatile write
int v = volatile *ptr;       // volatile read
```

- `&expr` takes an address; `*expr` dereferences.
- `cast<T>(expr)` performs an explicit cast. `expr as T` is parsed as exactly the same
  operation (postfix sugar for `cast<T>(expr)`), with `as` binding tighter than
  arithmetic but looser than postfix `.`/`[]`.
- `unsafe { ... }` blocks group operations the typechecker otherwise restricts (raw
  pointer dereferences in particular — most of `stdlib/`'s low-level byte-fiddling
  runs inside `unsafe` blocks).
- `volatile` before a `*`-dereference (read or write position) marks that specific
  load/store as volatile, both as a statement (`volatile *lhs = rhs;`) and as an
  expression (`volatile *ptr`).
- `size_of(T)` / `sizeof(T)` gives the byte size of any type: builtins (`sizeof(int)` is 8,
  `sizeof(uint8)` is 1, `sizeof(char)` is 1), aliases, and struct/class layouts
  (`sizeof(Header)`).
- `addrof_fn(name)` (also spelled `adrof_fn(name)` — both lex to the same token) takes
  the address of a named function.

## `naked` Functions and Inline Assembly

```hylian
naked void low_level() {
    asm {
        mov rax, 1
    }
}
```

An `asm { ... }` block (both `asm{` and `asm {` are matched literally by the lexer)
captures everything up to the **first** `}` character verbatim as the assembly body.
There's no brace-nesting or escaping inside it, so the body itself cannot contain a
literal `}` character — including inside a comment written within the block.

## Conditional Compilation: `@target(...)`

```hylian
@target(linux)
void platform_specific() {
    ...
}
```

`@target(name)` is handled entirely in the lexer, not the parser: it inspects the name
against the compiler's current `--target` value, and if they don't match, it skips
lexing the *single declaration that immediately follows* — either up to the next `;`
at brace-depth zero (a one-line declaration with no `{`), or until the matching `}`
that closes the first `{` it sees. It only makes sense directly before a top-level
function or class declaration, not inside expressions or statements.

## Modules and Includes

Covered in full in [Modules and Includes](modules.md). In brief:

```hylian
include {
    io,
    os.fs,
}
```

Each entry is a dotted path; dots become directory separators when resolving to a
`.hy` file on disk (`os.fs` → `os/fs.hy`) relative to the compiler's `--src-dir` (or
the including file's own directory as a fallback).

```hylian
module MathUtils {
    int square(int x) { return x * x; }          // private
    public int cube(int x) { return x * square(x); }
}
```

## C Interop

```hylian
ccpinclude "stdio.h";
```

Registers a C header for the FFI/vendor system to pull in. The fuller FFI story
(`.hyi` interface files, linking against native `.so`s) is covered in
[Vendor Packages & FFI](vendors.md).

## Error Handling

Covered in full in [Error Handling](error-handling.md), including current status —
`Error?` as a return type works; constructing and inspecting an actual error
(`Err(...)`, `.message()`, `.code()`) currently fails to link, same as `panic(...)`.

## Parser Error Recovery

A few grammar productions exist purely for error recovery, not normal use: a
malformed top-level function signature is resynchronized by skipping to the next
matching `}` (`error LBRACE stmt_list RBRACE`), a malformed class member is skipped up
to the next `;`, and a malformed statement is likewise skipped up to the next `;`.
These don't change what valid Hylian looks like; they just mean one bad declaration
doesn't cascade into a wall of unrelated parse errors.
