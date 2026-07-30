# Known Limitations

Hylian's grammar and standard library are moving faster than the codegen backend that
executes them. This page lists gaps that were confirmed by hand against the current
compiler (`compiler/parser.y`, `compiler/typecheck.c`, `compiler/lower.c`,
`compiler/ir_to_mir.c`) and the current `stdlib/` tree, so the rest of the reference can
describe the language honestly instead of aspirationally. Everything below was
reproduced with a real `./hylian ... -o out.o` + `gcc out.o ... -o bin` + run cycle.

If something here has since been fixed, trust the compiler over this page — this is a
snapshot, not a promise.

### A function call's result does not reliably survive a *later* call in the same function

This is the single biggest gap found in this pass, and it's not specific to any one
stdlib function — it's a register-allocation bug that affects ordinary function calls
in general. Minimal reproduction:

```hylian
int seven() {
    return 7;
}
int main() {
    int a = seven();
    if (a == 7) {
        return 1;    // this branch IS taken — using `a` right away works
    }
    return 0;
}
```

versus:

```hylian
int seven() {
    return 7;
}
int main() {
    int a = seven();
    return a;        // returns garbage (observed: 104), not 7
}
```

The difference is what happens between capturing the call's result and using it.
Every non-`naked` function — `main` here included — gets an automatically-inserted
call to `arena_free` right before it actually returns (part of the compiler-managed
arena lifecycle described in [`mem`](../stdlib/mem.md)). `arena_free`'s own generated
body reuses the same small set of scratch registers (`r11`–`r15`) that the caller was
using to hold `a` — confirmed by disassembling the linked binary: `a` sits in `r14`
after the call to `seven()`, and `arena_free`'s body unconditionally overwrites `r14`
several times before `main` reads it back out for the `return`. Nothing preserves it
across that call. The same thing happens with any other intervening call, not just
`arena_free` — two `naked` functions (no auto-inserted arena calls at all) calling each
other and both returning values works fine; it's specifically a live value in a
scratch register crossing *any* subsequent call, and `arena_free` is simply the call
that unconditionally exists at the end of every non-`naked` function, so it's the one
that bites almost everyone.

**Practical effect:** using a function's return value immediately (the same statement
or the next one, before any further call happens) is reliable. Storing it and using it
later — especially returning it, or passing it to another function, or printing it —
is not, once *any* further call has happened in between. This includes calling
stdlib functions from ordinary (non-`naked`) code and expecting to use what they hand
back — `int n = length(s); return n;` is exactly the broken shape above. Declaring the
caller `naked` sidesteps it (no `arena_free` call is inserted), at the cost of losing
the implicit arena for any `new` in that function.

Every worked example elsewhere in this documentation set that shows "call a function,
then do something with the result later" should be read with this in mind — it
describes the intended, sensible-looking code, not a guarantee that today's backend
gets the value from the call site to wherever it's used next.

### `i++`, `++i`, `i--`, `--i` do not change the variable

The postfix/prefix forms parse and typecheck, but the current backend does not lower
`IR_UNARY_OP`'s `++`/`--` cases into an actual read-modify-write — the statement is
effectively a no-op. A `while` loop that relies on `i++` to make progress will spin
forever. Use `i = i + 1` / `i = i - 1` instead until this is fixed.

### The `for` loop's post-clause can't be `i++` or `i += 1`

`compiler/parser.y`'s `for_init` rule (reused for both the init- and post-clause) only
accepts `type IDENT = expr`, `type IDENT`, `IDENT = expr`, or nothing — not an arbitrary
statement. So:

```hylian
for (int i = 0; i < 5; i++) { ... }   // syntax error: unexpected INC
```

is a parse error, not just dead code from the point above. Write the post-clause as a
plain assignment:

```hylian
for (int i = 0; i < 5; i = i + 1) { ... }
```

### Class fields don't work yet — implicit or explicit

Verified with both patterns:

```hylian
public class Player {
    public int health;
    Player(int h) { health = h; }       // implicit self-field write
    int get_health() { return health; } // implicit self-field read
}
```

fails typecheck outright (`undefined variable 'health'`) — `typecheck.c`'s
`infer_function` binds `self` into scope but never the class's fields. And:

```hylian
Point p = new Point();
p.x = 5;        // explicit member access
return p.x;
```

compiles, but fails at *link* time: `lower.c` lowers `obj.field` reads/writes to calls
named `<Class>_get_<field>`/`<Class>_set_<field>`, and nothing in the compiler ever
generates those functions. `nm` on the resulting object shows them as pure `U`ndefined
symbols.

Only zero-field classes (`new Empty()`) reliably work end to end today. Anything with
fields — the canonical `Player`/`BankAccount` kind of example — will not link.

### `Err(...)`, `panic(...)`, and `Error.message()`/`.code()` don't link

`Err()` and `panic()` are compiler builtins (special-cased in `typecheck.c`/`lower.c`,
not real stdlib functions), lowering to calls named `hylian_make_err` and
`hylian_panic`. Neither exists anywhere in `stdlib/`. The old C runtime
(`runtime/std/errors.o`) does define `hylian_panic`, `Error_message`, and `Error_code` —
but its error-*constructor* symbol is spelled `hylian_make_error` (with the `e`), one
letter off from what the current compiler calls, and even if you patch that mismatch,
`errors.o` itself depends on `hy_alloc`/`hy_write`/`hy_exit`, which aren't provided by
anything in the current toolchain either. Net effect: any program that calls `Err()`,
`panic()`, or `.message()`/`.code()` fails to link today. `Error?` as a return *type*
(just returning `nil` for success, never constructing an actual error) works fine.

### `array<T>` and `multi<A | B>` don't link

Array literals, `.push()`/`.pop()`/`.len`/`.cap`/indexing, and `multi<...>`
construction/`.tag`/`.value` all lower to runtime helper calls
(`hylian_array_alloc`, `hylian_array_push`, `hylian_array_get`, `hylian_multi_alloc`,
etc.) that are not defined anywhere in the codebase — not in `stdlib/`, not in the old
`runtime/` tree. Any program that uses `array<T>` or `multi<...>` fails to link with
undefined-reference errors.

### `EnumName.Variant` doesn't link

`compiler/lower.c` marks enum declarations as "metadata only — no IR instructions
needed," but `EnumName.Variant` member access still lowers to a `LEA` of a global named
`<Enum>_<Variant>`, which is never emitted. `switch`/`case` on a variant's plain integer
*value* works fine (`enum Color { Red, Green, Blue }` then `switch (c) { case 0: ... }`);
it's specifically writing `Color.Red` in source that produces a dangling symbol
reference at link time.

### `print`/`println` only reliably handle string literals

`ir_to_mir.c`'s print/println dispatch has four cases: a string literal (works — interned
directly), a `str`-typed variable (works via `strlen`, *when* the pointer traces back to
a literal), a bare `int` (attempted via `hylian_int_to_str` + a `MIR_ALLOCA_LOCAL` — but
`hylian_int_to_str` isn't defined anywhere in the current stdlib, and `MIR_ALLOCA_LOCAL`
is emitted by `ir_to_mir.c` but never consumed by the x64 backend, so this path fails at
both compile time and link time), and everything else — floats, interpolated strings —
which the code explicitly documents as unhandled and turns into a no-op with a stderr
warning ("not lowered yet: PRINTLN").

Beyond that: printing a string that was *computed at runtime* (the return value of
`stdlib/string.hy`'s `slice`, `concat`, `to_upper`, `to_lower`, `trim*`, or
`stdlib/io.hy`'s `int_to_str`) was observed to segfault in this pass — consistent with
[the register-clobbering issue above](#a-function-calls-result-does-not-reliably-survive-a-later-call-in-the-same-function):
the string pointer returned by `slice`/`int_to_str`/etc. sits in a scratch register
that a later call (printing it, which itself involves further calls, or even just the
enclosing function's own cleanup) can clobber before it's actually used. The same
functions were observed to work correctly when their result is used immediately
(e.g. measured with `length()` right away) rather than carried further. Treat any
example that prints a computed string as unverified until the root cause above is
fixed.

### String interpolation (`"...{{expr}}..."`) only keeps the literal parts

`ast.c`'s `make_interp_string` splits an interpolated string into literal and
expression segments correctly, but `lower.c` hands each `{{expr}}` segment to
`ir_to_mir.c` as **raw, unparsed source text** — `ir_to_mir.c` has no expression
evaluator available to it (that's `lower.c`'s job, and it doesn't do it), so every
expression segment is silently dropped from the result (with a stderr diagnostic naming
the segment), leaving only the literal text. And as noted above, even that computed
string currently can't be passed straight to `print`/`println` — only to a variable.

### There is no tuple / multi-return type

`ast.h` explicitly notes tuple support "was unimplemented" and has been removed
entirely. `(int, int) divmod(...)` style multi-return syntax you may see referenced
elsewhere does not exist in the current grammar — functions return exactly one `type`.

---

**What this leaves as solid ground today:** primitive scalar types and arithmetic on
local variables, `if`/`else`/`while`/`for` (C-style, with the post-clause caveat
above)/`for`-`in`, `switch`/`case`/`default`, `unsafe` blocks, `naked` functions used
to sidestep the register-clobbering issue above, pointers (`&`/`*`/`cast<T>`/`as`),
`include`/`module`, zero-field classes, enums compared by integer value, and printing
string literals — *plus* calling a function and using its result **immediately**
(same or next statement, no further call in between). Using a function's result any
later than that — across a further call, or by returning it — hits the
register-clobbering issue described first on this page, which cuts across nearly
everything else, including most stdlib functions called from ordinary (non-`naked`)
code. Most of the rest of this reference describes what the *grammar* supports even
where a note above says the backend isn't there yet — that's the point of this page:
read the feature docs for the design, read this page for what's actually load-bearing
right now.
