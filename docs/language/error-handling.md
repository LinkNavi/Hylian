# Error Handling

Hylian's error model is built around `Error?` as a return type, plus two compiler
builtins — `Err(...)` to construct an error and `panic(...)` to abort. There's no
exception system and no stack unwinding.

```hylian
Error? parse(str s) {
    if (length(s) == 0) { return Err("empty input", 22); }   // message, optional int code
    return nil;
}

Error? e = parse("");
if (e) { println("failed: {{e.message()}} (code {{e.code()}})"); }
```

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
| `.code()` | `int` | The integer code from `Err(msg, code)`; `0` for `Err(msg)`. |

## `panic`

`panic(msg)` prints `panic: msg` to stderr and exits with status 1 (no unwinding; `defer`red
code does not run) — intended for unrecoverable situations
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
