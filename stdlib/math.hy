// math — integer and floating-point helpers.
//
//   int hi = max(3, 9);            // 9
//   float r = sqrt(2.0);           // 1.4142135623730951 (prints 1.414214)
//   float a = fabs(-2.5);          // 2.5
//
// The integer functions take and return `int`; the float functions take and
// return `float` (64-bit). They're plain Hylian on top of the compiler's
// scalar float support, so accuracy is "good enough for graphics and
// scripting" (roughly 1e-12 relative for the transcendental ones), not
// libm-grade, and there are no separate float32 variants - a float32 argument
// is widened automatically.

static float PI      = 3.14159265358979323846;
static float TAU     = 6.28318530717958647692;
static float E       = 2.71828182845904523536;
static float LN2     = 0.69314718055994530942;
static float SQRT2   = 1.41421356237309504880;

// ── integers ─────────────────────────────────────────────────────────────────

int min(int a, int b) { return a < b ? a : b; }
int max(int a, int b) { return a > b ? a : b; }
int abs(int x) { return x < 0 ? -x : x; }
int sign(int x) { return x > 0 ? 1 : (x < 0 ? -1 : 0); }

// clamp: x limited to the range [lo, hi].
int clamp(int x, int lo, int hi) {
    if (x < lo) { return lo; }
    if (x > hi) { return hi; }
    return x;
}

// ipow: base raised to a non-negative integer power (overflow wraps).
int ipow(int base, int exp) {
    int result = 1;
    while (exp > 0) {
        if ((exp & 1) == 1) { result = result * base; }
        base = base * base;
        exp = exp >> 1;
    }
    return result;
}

// isqrt: floor of the square root of a non-negative integer.
int isqrt(int n) {
    if (n < 2) { return n < 0 ? 0 : n; }
    int lo = 1;
    int hi = n < 3037000499 ? n : 3037000499; // sqrt(2^63)
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (mid <= n / mid) { lo = mid; } else { hi = mid - 1; }
    }
    return lo;
}

int gcd(int a, int b) {
    a = abs(a);
    b = abs(b);
    while (b != 0) {
        int t = a % b;
        a = b;
        b = t;
    }
    return a;
}

// ── floats ───────────────────────────────────────────────────────────────────

float fmin(float a, float b) { return a < b ? a : b; }
float fmax(float a, float b) { return a > b ? a : b; }
float fabs(float x) { return x < 0.0 ? 0.0 - x : x; }

float fclamp(float x, float lo, float hi) {
    if (x < lo) { return lo; }
    if (x > hi) { return hi; }
    return x;
}

// is_nan: true for NaN (the only value not equal to itself).
bool is_nan(float x) { return x != x; }

// floor / ceil / round / trunc. Values beyond 2^62 are already integral, and
// are returned unchanged (converting them to int would overflow).
float trunc(float x) {
    if (x > 4.0e18 || x < -4.0e18 || x != x) { return x; }
    return cast<float>(cast<int>(x));
}

float floor(float x) {
    float t = trunc(x);
    if (t > x) { return t - 1.0; }
    return t;
}

float ceil(float x) {
    float t = trunc(x);
    if (t < x) { return t + 1.0; }
    return t;
}

// round: nearest integer, halves away from zero.
float round(float x) {
    if (x < 0.0) { return 0.0 - floor(0.5 - x); }
    return floor(x + 0.5);
}

// sqrt: square root by Newton's method; negative input gives NaN.
float sqrt(float x) {
    if (x != x || x < 0.0) { return 0.0 / 0.0 * 0.0 + (0.0 - 1.0) * 0.0 / 0.0; }
    if (x == 0.0) { return 0.0; }
    float g = x > 1.0 ? x / 2.0 : 1.0;
    int i = 0;
    while (i < 80) {
        float next = 0.5 * (g + x / g);
        if (fabs(next - g) <= 1.0e-15 * next) { return next; }
        g = next;
        i++;
    }
    return g;
}

// fpow: x raised to an integer power (negative allowed).
float fpow(float x, int n) {
    if (n < 0) { return 1.0 / fpow(x, 0 - n); }
    float result = 1.0;
    float b = x;
    while (n > 0) {
        if ((n & 1) == 1) { result = result * b; }
        b = b * b;
        n = n >> 1;
    }
    return result;
}

// _reduce: x brought into [-PI, PI] by removing whole turns.
static float _reduce(float x) {
    if (x > PI || x < 0.0 - PI) {
        float turns = floor(x / TAU + 0.5);
        x = x - turns * TAU;
    }
    return x;
}

float sin(float x) {
    x = _reduce(x);
    float term = x;
    float sum = x;
    int n = 1;
    while (n < 14) {
        term = 0.0 - term * x * x / cast<float>((2 * n) * (2 * n + 1));
        sum = sum + term;
        n++;
    }
    return sum;
}

float cos(float x) {
    x = _reduce(x);
    float term = 1.0;
    float sum = 1.0;
    int n = 1;
    while (n < 14) {
        term = 0.0 - term * x * x / cast<float>((2 * n - 1) * (2 * n));
        sum = sum + term;
        n++;
    }
    return sum;
}

float tan(float x) { return sin(x) / cos(x); }

// exp: e^x, via x = k*ln2 + r with |r| <= ln2/2 and a Taylor series for e^r.
float exp(float x) {
    if (x > 709.0) { return 1.0e308 * 10.0; }
    if (x < -745.0) { return 0.0; }
    float k = round(x / LN2);
    float r = x - k * LN2;
    float term = 1.0;
    float sum = 1.0;
    int n = 1;
    while (n < 20) {
        term = term * r / cast<float>(n);
        sum = sum + term;
        n++;
    }
    int ki = cast<int>(k);
    float scale = fpow(2.0, ki);
    return sum * scale;
}

// ln: natural logarithm (x > 0). Reduces x to m * 2^e with m in [0.75, 1.5),
// then ln(m) = 2 * atanh((m-1)/(m+1)).
float ln(float x) {
    if (x <= 0.0 || x != x) { return 0.0 - 1.0e308 * 10.0; }
    int e = 0;
    while (x >= 1.5) { x = x / 2.0; e++; }
    while (x < 0.75) { x = x * 2.0; e--; }
    float y = (x - 1.0) / (x + 1.0);
    float y2 = y * y;
    float term = y;
    float sum = 0.0;
    int n = 0;
    while (n < 30) {
        sum = sum + term / cast<float>(2 * n + 1);
        term = term * y2;
        n++;
    }
    return 2.0 * sum + cast<float>(e) * LN2;
}

float log10(float x) { return ln(x) / 2.302585092994046; }
float log2(float x) { return ln(x) / LN2; }

// pow: x^y for real y (x > 0), falling back to repeated multiplication when
// y is a whole number so negative bases work too.
float pow(float x, float y) {
    if (y == trunc(y) && fabs(y) < 1.0e9) { return fpow(x, cast<int>(y)); }
    return exp(y * ln(x));
}

// atan: arctangent, via argument reduction to [0, tan(pi/12)] and a series.
float atan(float x) {
    if (x < 0.0) { return 0.0 - atan(0.0 - x); }
    if (x > 1.0) { return PI / 2.0 - atan(1.0 / x); }
    if (x > 0.2679491924311227) {
        // atan(x) = pi/6 + atan((x*sqrt(3) - 1) / (sqrt(3) + x))
        float s3 = 1.7320508075688772;
        return PI / 6.0 + atan((x * s3 - 1.0) / (s3 + x));
    }
    float x2 = x * x;
    float term = x;
    float sum = x;
    int n = 1;
    while (n < 20) {
        term = 0.0 - term * x2;
        sum = sum + term / cast<float>(2 * n + 1);
        n++;
    }
    return sum;
}

float atan2(float y, float x) {
    if (x > 0.0) { return atan(y / x); }
    if (x < 0.0) { return y >= 0.0 ? atan(y / x) + PI : atan(y / x) - PI; }
    if (y > 0.0) { return PI / 2.0; }
    if (y < 0.0) { return 0.0 - PI / 2.0; }
    return 0.0;
}

float deg_to_rad(float d) { return d * PI / 180.0; }
float rad_to_deg(float r) { return r * 180.0 / PI; }
