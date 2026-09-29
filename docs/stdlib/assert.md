# `assert`

```hylian
include { assert, }
```

Source: `stdlib/assert.hy`. Cheap runtime checks. A failed check prints
`assertion failed: <message>` (with both values for the `assert_eq` forms) to stderr and
exits with status 1. The message is required — Hylian can't capture the source text of the
condition.

| Function | Signature | Description |
|---|---|---|
| `assert` | `void assert(bool cond, str msg)` | Fail unless `cond` is true. |
| `assert_eq` | `void assert_eq(int actual, int expected, str msg)` | Fail unless the ints are equal. |
| `assert_eq_str` | `void assert_eq_str(str actual, str expected, str msg)` | Fail unless the strings are equal. |
| `assert_near` | `void assert_near(float actual, float expected, float eps, str msg)` | Fail unless `\|actual - expected\| <= eps`. |

```hylian
include { assert, }

int main() {
    assert(n > 0, "n must be positive");
    assert_eq(add(2, 3), 5, "add");
    return 0;
}
```
