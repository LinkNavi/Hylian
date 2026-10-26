// C side of the float-FFI test: a function Hylian calls, which in turn calls
// back into a Hylian function, both passing/returning floats per the SysV ABI.
double hy_scale(double a, int n, double b, float f);   /* defined in ffi_float.hy */

double c_mix(double a, int n, double b, float f) { return a * 100 + n * 10 + b + f / 4; }

double c_call_back(double x) { return hy_scale(x, 3, 0.5, 2.0f) + 1000; }

int c_neg(int x) { return -x; }                       /* upper half of rax is not sign-extended */
unsigned int c_u32(unsigned int v) { return v + 1u; } /* wraps at 32 bits */
float c_halff(float v) { return v / 2.0f; }

/* takes a function pointer: C calls back into Hylian through it */
long c_apply(long (*f)(long, long), long a, long b) { return f(a, b) + 1; }

/* ---- structs by value (SysV classification) ---- */
typedef struct { float x, y; } Vec2;                 /* one SSE eightbyte  */
typedef struct { unsigned char r, g, b, a; } Col;    /* one INTEGER eightbyte, 4 bytes */
typedef struct { int x, y, w, h; } Rect;             /* two INTEGER eightbytes */
typedef struct { float x, y, z; } Vec3;              /* SSE + SSE(4 bytes) */
typedef struct { int a, b, c, d, e; } Big;           /* 20 bytes: MEMORY */
typedef struct { double d; long l; } Mix;            /* SSE + INTEGER */

Vec2 c_v2_add(Vec2 a, Vec2 b) { Vec2 r = { a.x + b.x, a.y + b.y }; return r; }
Col c_col_dim(Col c, float k) { Col r = { (unsigned char)(c.r * k), (unsigned char)(c.g * k), (unsigned char)(c.b * k), c.a }; return r; }
int c_rect_area(Rect r) { return r.w * r.h; }
Rect c_rect_grow(Rect r, int n) { Rect o = { r.x - n, r.y - n, r.w + 2 * n, r.h + 2 * n }; return o; }
float c_v3_len2(Vec3 v) { return v.x * v.x + v.y * v.y + v.z * v.z; }
Vec3 c_v3_scale(Vec3 v, float k) { Vec3 r = { v.x * k, v.y * k, v.z * k }; return r; }
int c_big_sum(Big b) { return b.a + b.b * 10 + b.c * 100 + b.d * 1000 + b.e * 10000; }
Big c_big_make(int base) { Big b = { base, base + 1, base + 2, base + 3, base + 4 }; return b; }
Mix c_mix_swap(Mix m) { Mix r = { (double)m.l, (long)m.d }; return r; }
/* five int args use 5 of 6 integer registers, so the 2-eightbyte Rect no
   longer fits in registers and goes wholly on the stack */
int c_spill(int a, int b, int c, int d, int e, Rect r) { return a + b + c + d + e + r.x + r.y * 10 + r.w * 100 + r.h * 1000; }
