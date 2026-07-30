# Syntax Reference

This page documents the Hylian language grammar as implemented in
`compiler/parser.y`, `compiler/lexer.l`, and `compiler/ast.h` — not an idealized design.
Where the parser accepts something the current backend can't yet execute, there's a
pointer to [Known Limitations](known-limitations.md) rather than a silent omission.

## File Structure

A `.hy` file is a flat sequence of top-level items: an optional `include { ... }`
block, any number of `ccpinclude` lines, and any number of `class`, `enum`, `module`,
function, or `static`/`const` variable declarations, in any order. There's no required
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

Only single-line comments are supported, starting with `//` and running to end of
line. There is no block-comment syntax (`/* ... */` is not recognized).

## String and Character Literals

A string literal is `"..."` with no escape-sequence processing at all: the lexer
matches `\"([^\\\"]|\\.)*\"` (so a backslash lets you include a literal `"` without
ending the string), but nothing later in the compiler interprets `\n`, `\t`, `\\`, etc.
`"\n"` is the two raw bytes `\` and `n`, not a newline byte. `stdlib/runtime.hy`'s
implementation of the `println` builtin works around this by storing byte value `10`
in a local and writing it directly, rather than relying on a `"\n"` literal — that's
the idiomatic way to emit a newline byte from source today. There is no separate
character-literal syntax; single bytes are written as small integers cast to `uint8`.

## String Interpolation

A string literal containing `{{...}}` anywhere is lexed as a distinct interpolated
string rather than a plain literal, and split into alternating literal/expression
segments. As of today, only the **literal** segments make it into the compiled result —
each `{{expr}}` segment is handed from `lower.c` to the codegen backend as raw,
unparsed source text with no evaluator on the receiving end, so it's dropped (with a
diagnostic naming the segment) rather than evaluated. See
[Known Limitations](known-limitations.md) for the current status, including that
passing an interpolated string straight to `print`/`println` doesn't work at all yet
(only assigning it to a variable partially works, keeping just the literal text).

## Numeric Literals

- Decimal integers: `42`, with optional `_` digit separators anywhere (`1_000_000`).
- Hexadecimal integers: `0x` or `0X` followed by hex digits, also with optional `_`
  separators (`0xFF_FF`).
- Floats: `[0-9]+.[0-9]+` only — a digit on both sides of the dot. There is no
  exponent notation (`1e10`) and no bare leading/trailing dot (`.5` or `5.` are not
  recognized as floats).
- Booleans: `true`, `false`.
- Null: both `nil` and `null` are accepted and produce the same literal.

## Types

| Category | Syntax | Notes |
|---|---|---|
| Integer | `int` | Signed, general-purpose integer. |
| Sized integers | `int8`, `int16`, `int32`, `int64`, `uint8`, `uint16`, `uint32`, `uint64` | Recognized by the typechecker as primitives (`typecheck.c`'s `is_known_primitive`), spelled as ordinary identifiers — the lexer has no dedicated tokens for these, so they parse via the generic `IDENTIFIER` type rule. |
| String | `str` | A pointer to a nul-terminated byte buffer; `str` values are raw pointers under the hood. |
| Boolean | `bool` | `true` / `false`. |
| Void | `void` | Function/method return type only. |
| Error | `Error` | See [Error Handling](error-handling.md). |
| Float | `float`, `float32`, `float64` | Recognized as primitives; arithmetic/printing support is limited today — see [Known Limitations](known-limitations.md). |
| Pointer-sized | `usize`, `isize` | Unsigned/signed pointer-sized integers, used throughout the stdlib for raw addresses. |
| Auto | *(no keyword)* | Only reachable via `name := expr;` — there is no explicit `auto` type you can write in a signature. |
| Class / enum name | any `IDENTIFIER` | A user-defined class or enum used as a type. |
| Reference | `&T` | `TYPE_REF` — e.g. `&int`. |
| Raw pointer | `*T` | `TYPE_RAWPTR` — e.g. `*uint8`. |
| Nullable | `T?` | A `?` suffix after any base type marks it nullable (allows `nil`). Valid on `var_decl` and on function/method return types (`Error? main()`). |
| Fixed array | `array<T, N>` | Heap-backed, fixed capacity `N`. |
| Growable array | `array<T>` | Heap-backed, flexible capacity. |
| Tagged union | `multi<A \| B \| ...>` | Optionally `multi<A \| B \| ..., N>` for a fixed backing size. |
| `any` union | `multi<any>` | Optionally `multi<any, N>`. |

Arrays and `multi` are described further in [Arrays and Tagged Unions](#arrays-and-tagged-unions-multi)
below — both parse and typecheck, but currently fail to *link* (see
[Known Limitations](known-limitations.md)).

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
| 1 | `=` `+=` `-=` `*=` `/=` `%=` | right |
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

Compound assignment (`+=` etc.) and plain declare-assign (`:=`) only target a bare
identifier — there's no `arr[i] += 1` or `obj.field += 1` form in the grammar. Plain
`=` assignment does additionally support a member target (`obj.field = expr;`) and an
index target (`arr[i] = expr;`), just not the compound forms.

Increment/decrement (`++x`, `x++`, `--x`, `x--`) parse in both prefix and postfix,
expression and statement position — but see
[Known Limitations](known-limitations.md): the current backend doesn't actually
perform the mutation.

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

Parameters are a comma-separated `type IDENTIFIER` list; there's no default-argument
syntax and no variadic parameters.

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

Three-clause C-style, plus a separate `for`-`in` form.

```hylian
for (int i = 0; i < 5; i = i + 1) {
    ...
}

for (n in nums) {           // by value
    ...
}

for (&n in nums) {          // by reference — n is effectively a pointer to the element
    *n = *n * 2;
}
```

The init- and post-clauses share one grammar rule (`for_init`) that only accepts
`type IDENT = expr`, `type IDENT`, `IDENT = expr`, or nothing at all. **`for (int i = 0; i < 5; i++)`
is a syntax error** — `i++` isn't a valid post-clause. Use `i = i + 1`. The init- and
post-clauses may each be omitted; the middle (condition) clause has no "empty"
alternative in the grammar, so write an explicit `true` for an infinite loop:
`for (; true ;) { ... }`.

`for`-`in`'s collection is expected to be an `array<T>` — see
[Known Limitations](known-limitations.md) for the current state of `array<T>`.

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

A variant with no explicit value gets its **zero-based position in the declaration**
as its value — not one more than the previous variant's value. This differs from C:

```hylian
enum E { A, B = 10, C }
// A = 0 (position 0)
// B = 10 (explicit)
// C = 2 (position 2 — NOT 11)
```

Enums are `public` or private-to-file, same rule as classes. Member access
(`Status.Ok`) parses and typechecks but doesn't currently link — see
[Known Limitations](known-limitations.md); comparing/switching on a variant's raw
integer value works today.

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
- `size_of(Name)` takes a bare identifier — because it's grammared as
  `SIZE_OF LPAREN IDENTIFIER RPAREN`, and built-in primitive type keywords like `int`
  and `str` are their own tokens (not `IDENTIFIER`), `size_of(int)` is a **syntax
  error**. `size_of` only accepts a user-defined class/enum name, e.g.
  `size_of(Player)`.
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
