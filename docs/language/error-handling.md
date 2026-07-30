# Error Handling

Hylian's error model is built around `Error?` as a return type, plus two compiler
builtins — `Err(...)` to construct an error and `panic(...)` to abort. There's no
exception system and no stack unwinding.

> **Current status:** `Error?` as a return type (returning `nil` for success) works
> today. Actually constructing an error with `Err(...)`, calling `panic(...)`, or
> calling `.message()`/`.code()` on an error value does **not** currently link — see
> [Known Limitations](known-limitations.md#errnil-panic-and-errormessagecode-dont-link)
> for exactly why (a missing/misnamed runtime symbol and, for `panic`, a chain of
> further missing dependencies even in the old C runtime). The patterns below
> describe the intended design as the parser and typechecker implement it; treat them
> as not-yet-runnable until that's fixed.

## The `Error?` Type

`Error?` is `Error` with the nullable (`?`) modifier — `nil` means success, non-nil
means an `Error` value carrying details. Any function that can fail declares `Error?`
as its return type:

```hylian
Error? writeFile(str path) {
    // ...
    return nil;
}

// Cannot fail — no need for Error?
int add(int a, int b) {
    return a + b;
}
```

## Returning Errors

```hylian
Error? validateUsername(str name) {
    if (is_empty(name)) {
        return Err("username cannot be empty");
    }
    return nil;
}
```

`Err(msg)` and `panic(msg)` are recognized by name directly in `typecheck.c` and
`lower.c` — they are not ordinary stdlib functions you `include`, and there's no
`Error` class declared anywhere in `stdlib/` to look at; the compiler special-cases
both call names.

## Checking Errors

```hylian
Error? err = validateUsername("Alice");
if (err) {
    panic(err.message());
}
// reaching here means err == nil
```

A non-nil `Error?` is truthy in an `if`; `nil` is falsy.

## `Error`'s Methods

| Method | Return type | Description |
|---|---|---|
| `.message()` | `str` | The string passed to `Err(...)`. |
| `.code()` | `int` | An integer error code. |

## `panic`

`panic(msg)` prints to stderr and terminates — intended for unrecoverable situations
(a programming mistake, a state the program genuinely can't continue from), not for
expected failure conditions like a missing file or bad user input, which should
propagate as `Error?` instead.

## Propagating and Handling

```hylian
Error? processFile(str path) {
    Error? err = readFile(path);
    if (err) {
        return err;             // propagate
    }
    return nil;
}

Error? run() {
    Error? err = connectToServer("example.com");
    if (err) {
        println("Could not connect, falling back to offline mode");
        // handle locally, fall through
    }
    return nil;
}

Error? main() {
    Error? err = initSubsystem();
    if (err) {
        panic(err.message());   // unrecoverable — bail out
    }
    return nil;
}
```

## Error Handling with Classes

Methods return `Error?` the same way free functions do:

```hylian
public class BankAccount {
    private int balance;

    BankAccount(int initialBalance) {
        balance = initialBalance;
    }

    Error? withdraw(int amount) {
        if (amount > balance) {
            return Err("insufficient funds");
        }
        balance = balance - amount;
        return nil;
    }
}
```

Note this example also depends on class field access (`balance`), which — independent
of the `Error?`/`Err()` issues above — has its own current gap; see
[Known Limitations](known-limitations.md#class-fields-dont-work-yet--implicit-or-explicit).
