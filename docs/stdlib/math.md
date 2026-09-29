# `math`

```hylian
include { math, }
```

Source: `stdlib/math.hy`. Integer helpers and floating-point functions, written in Hylian
on top of the compiler's scalar float support.

## Integers

| Function | Signature | Description |
|---|---|---|
| `min` / `max` | `int min(int a, int b)` | Smaller / larger of two. |
| `abs` | `int abs(int x)` | Absolute value. |
| `sign` | `int sign(int x)` | `-1`, `0` or `1`. |
| `clamp` | `int clamp(int x, int lo, int hi)` | `x` limited to `[lo, hi]`. |
| `ipow` | `int ipow(int base, int exp)` | Integer power (`exp >= 0`, overflow wraps). |
| `isqrt` | `int isqrt(int n)` | Floor of the square root. |
| `gcd` | `int gcd(int a, int b)` | Greatest common divisor. |

## Floats

Constants: `PI`, `TAU`, `E`, `LN2`, `SQRT2`.

| Function | Description |
|---|---|
| `fmin`, `fmax`, `fabs`, `fclamp` | Float versions of the above. |
| `trunc`, `floor`, `ceil`, `round` | Rounding (`round` sends halves away from zero). |
| `sqrt` | Square root (Newton's method). |
| `fpow(x, int n)`, `pow(x, y)` | Integer and real powers. |
| `sin`, `cos`, `tan`, `atan`, `atan2` | Trigonometry, in radians. |
| `exp`, `ln`, `log2`, `log10` | Exponential and logarithms. |
| `deg_to_rad`, `rad_to_deg` | Angle conversion. |
| `is_nan` | True for NaN. |

Accuracy is roughly 1e-12 relative for the transcendental functions — fine for graphics and
scripting, not a libm replacement.

```hylian
include { math, }

int main() {
    float r = sqrt(2.0);
    println("sqrt2={{r}} sin(pi/6)={{sin(PI / 6.0)}} max={{max(3, 9)}}");
    return 0;
}
```
