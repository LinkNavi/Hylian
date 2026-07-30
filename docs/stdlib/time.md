# `time`

```hylian
include { time, }
```

Source: `stdlib/time.hy`. Wall-clock time and sleep, via `clock_gettime`/`nanosleep`.

| Function | Signature | Description |
|---|---|---|
| `now` | `int now()` | Current wall-clock time in whole seconds since the Unix epoch. |
| `nanos` | `int nanos()` | Current wall-clock time in nanoseconds since the epoch. |
| `millis` | `int millis()` | Current wall-clock time in milliseconds since the epoch — the granularity most application code wants. |
| `sleep` | `void sleep(int ms)` | Pause the calling thread for `ms` milliseconds. |

```hylian
include { time, io, }

int main() {
    int start = millis();
    sleep(100);
    int elapsed = millis() - start;
    return 0;
}
```

There's no real `struct timespec` type wired up in the language yet, so every function
here that needs one allocates a fresh 16-byte buffer and reads/writes the two `int64`
fields (`tv_sec`, `tv_nsec`) directly via pointer arithmetic.
