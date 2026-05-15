# Error Handling in Hylian

Hylian uses a lightweight, explicit error handling model built around the `Error?` type. There is no exception system and no stack unwinding — errors are values, and the programmer decides what to do with them at every call site.

---

## The `Error?` Type

`Error?` is a nullable pointer to an `Error` object.

- `nil` — no error occurred; the operation succeeded
- non-nil — an error occurred; the value points to an `Error` object carrying details

Because `Error?` is just a nullable type, it composes naturally with Hylian's type system. Any function that may fail declares `Error?` as its return type. Functions that cannot fail use a concrete return type or `void`.

```hylian
// This function may fail
Error? writeFile(str path) {
    // ...
}

// This function cannot fail
int add(int a, int b) {
    return a + b;
}
```

---

## Returning Errors

Inside a function that returns `Error?`, use `return Err("message")` to signal failure and `return nil` to signal success.

### Signalling failure

```hylian
Error? validateUsername(str name) {
    if (name == "") {
        return Err("username cannot be empty");
    }
    return nil;
}
```

### Signalling success

```hylian
Error? connect(str host) {
    // ... connection logic ...
    return nil;
}
```

The error message passed to `Err()` should be a human-readable description of what went wrong. It is accessible to callers via `.message()`.

---

## Checking Errors

The return value of an `Error?`-returning function should be captured in a variable and then checked with an `if` statement. A non-nil `Error?` is truthy; `nil` is falsy.

```hylian
Error? err = validateUsername("Alice");
if (err) {
    // err is non-nil — something went wrong
    panic(err.message());
}
// reaching here means err == nil, i.e. success
```

You can also check inline without naming the variable, though capturing it is required if you want to inspect the message:

```hylian
Error? err = writeFile("/tmp/out.txt");
if (err) {
    panic(err.message());
}
```

---

## The `Error` Object

When `Error?` is non-nil, it points to an `Error` object with the following methods:

| Method | Return type | Description |
|---|---|---|
| `.message()` | `str` | Returns the human-readable error string passed to `Err()` |
| `.code()` | `int` | Returns an integer error code. Currently always `0` |

```hylian
Error? err = doThing();
if (err) {
    str msg = err.message();
    int code = err.code();
    println("Error {{code}}: {{msg}}");
}
```

> **Note:** `.code()` always returns `0` in the current version of the compiler. It is reserved for future use when numeric error codes are assigned to standard library errors.

---

## `panic`

`panic` is used for unrecoverable errors — situations where the program cannot safely continue. It prints its message to stderr and immediately exits with status code `1`. It never returns.

```hylian
panic("out of memory");
```

`panic` accepts a `str` argument. A common pattern is to pass `err.message()` directly:

```hylian
Error? err = connect("example.com");
if (err) {
    panic(err.message());
}
```

Use `panic` when:
- An error represents a programming mistake (e.g. an invalid argument that should never occur)
- The program has entered a state it cannot recover from
- You are in a context where propagating an error upward is not possible

Do not use `panic` for expected failure conditions (e.g. a file not found, a bad user input). Those should be propagated as `Error?` return values so the caller can decide what to do.

---

## The Full Pattern

The idiomatic Hylian error handling pattern is:

1. A function that may fail returns `Error?`
2. It returns `Err("description")` on failure and `nil` on success
3. The caller captures the return value, checks it with `if (err)`, and either handles the error, propagates it, or panics

### Propagating an error upward

If the current function also returns `Error?`, you can propagate an error by returning it directly:

```hylian
Error? processFile(str path) {
    Error? err = readFile(path);
    if (err) {
        return err;
    }

    Error? parseErr = parseContents(path);
    if (parseErr) {
        return parseErr;
    }

    return nil;
}
```

### Handling an error locally

```hylian
Error? run() {
    Error? err = connectToServer("example.com");
    if (err) {
        println("Could not connect: {{err.message()}}. Using offline mode.");
        // fall through to offline logic
    }
    return nil;
}
```

### Panicking on unrecoverable errors

```hylian
Error? main() {
    Error? err = initSubsystem();
    if (err) {
        panic(err.message());
    }
    return nil;
}
```

---

## Error Handling with Classes

Methods on classes can return `Error?` just like free functions. The caller checks the result the same way.

### Defining a method that may fail

```hylian
public class BankAccount {
    private int balance;

    BankAccount(int initialBalance) {
        balance = initialBalance;
    }

    int getBalance() {
        return balance;
    }

    Error? withdraw(int amount) {
        if (amount <= 0) {
            return Err("withdrawal amount must be positive");
        }
        if (amount > balance) {
            return Err("insufficient funds");
        }
        balance = balance - amount;
        return nil;
    }

    Error? deposit(int amount) {
        if (amount <= 0) {
            return Err("deposit amount must be positive");
        }
        balance = balance + amount;
        return nil;
    }
}
```

### Calling a method that may fail

```hylian
BankAccount account = new BankAccount(500);

Error? err = account.withdraw(200);
if (err) {
    println("Withdrawal failed: {{err.message()}}");
}

Error? err2 = account.withdraw(1000);
if (err2) {
    panic(err2.message());
}
```

### Propagating from a method

A method on one class can call a method on another and propagate the error upward:

```hylian
public class PaymentProcessor {
    private BankAccount source;

    PaymentProcessor(BankAccount acct) {
        source = acct;
    }

    Error? charge(int amount) {
        Error? err = source.withdraw(amount);
        if (err) {
            return err;
        }
        // ... process the charge ...
        return nil;
    }
}
```
