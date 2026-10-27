typedef struct { float x, y; } In;
typedef struct { In p; int id; } Outer;
typedef struct { char c; double d; } Padded;
typedef struct { int v[3]; int n; } WithArr;
typedef union { int i; float f; } Num;
typedef struct { int tag; Num u; } Tagged;
typedef struct { int x, y, w, h; } Rect2;
typedef struct { Rect2 a, b; } Quad;

float n_outer_sum(Outer o) { return o.p.x + o.p.y + (float)o.id; }
Outer n_outer_make(float x, int id) { Outer o = { { x, x * 2 }, id }; return o; }
double n_padded_sum(Padded p) { return p.c + p.d; }
Padded n_padded_make(int c, double d) { Padded p = { (char)c, d }; return p; }
int n_arr_sum(WithArr a) { return a.v[0] + a.v[1] * 10 + a.v[2] * 100 + a.n * 1000; }
WithArr n_arr_make(int base) { WithArr a = { { base, base + 1, base + 2 }, base + 3 }; return a; }
int n_tag_int(Tagged t) { return t.tag * 100 + t.u.i; }
int n_quad_area(Quad q) { return q.a.w * q.a.h + q.b.w * q.b.h; }
Quad n_quad_make(int k) { Quad q = { { k, k, k + 1, k + 2 }, { 1, 2, 3, 4 } }; return q; }
