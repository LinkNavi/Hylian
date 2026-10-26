include { std.io, vendors.cmath, }

// Called from C (ffi_float.c): mixed double / int / double / float32 arguments.
int hy_sub(int a, int b) { return a - b; }
float hy_scale(float a, int n, float b, float32 f) { return a * n + b + f; }

int main() {
    println("sqrt={{sqrt(2.0)}} pow={{pow(2.0, 10.0)}} floor={{floor(-2.5)}} fabs={{fabs(-7.25)}}");
    println("int args promoted: {{sqrt(16)}} {{pow(2, 5)}}");
    println("ldexp={{ldexp(1.5, 4)}} hypot={{hypot(3.0, 4.0)}}");
    float32 fm = fmaf(2.0, 3.0, 0.5);
    println("float32: {{fm}}");
    println("c_mix={{c_mix(1.5, 2, 0.25, 8.0)}}");
    println("callback={{c_call_back(4.0)}}");

    // variadic C call: the xmm count travels in %al
    str buf = concat("", "................................");
    snprintf(buf, 32, "%.3f/%d", 3.14159, 42);
    println(buf);

    // 32-bit C ints: results are sign/zero-extended, not read as 64-bit garbage
    println("c ints: {{c_neg(5)}} {{c_neg(-7)}} {{c_u32(4294967295)}} {{c_halff(5.0)}}");

    println("callback ptr={{c_apply(addrof_fn(hy_sub), 50, 8)}}");

    // structs by value: SysV classification, in and out
    Vec2 va; va.x = 1.5; va.y = 2.0;
    Vec2 vb; vb.x = 0.25; vb.y = 4.0;
    Vec2 vs = c_v2_add(va, vb);
    println("vec2: {{vs.x}} {{vs.y}}");
    Col col; col.r = 200; col.g = 100; col.b = 50; col.a = 255;
    Col cd = c_col_dim(col, 0.5);
    println("col: {{cd.r}} {{cd.g}} {{cd.b}} {{cd.a}}");
    Rect rc; rc.x = 1; rc.y = 2; rc.w = 30; rc.h = 4;
    Rect rg = c_rect_grow(rc, 2);
    println("rect: {{c_rect_area(rc)}} {{rg.x}} {{rg.y}} {{rg.w}} {{rg.h}}");
    Vec3 v3; v3.x = 1.0; v3.y = 2.0; v3.z = 3.0;
    Vec3 v3s = c_v3_scale(v3, 2.0);
    println("vec3: {{c_v3_len2(v3)}} {{v3s.x}} {{v3s.y}} {{v3s.z}}");
    Big bg = c_big_make(1);
    println("big: {{c_big_sum(bg)}} {{bg.a}} {{bg.e}}");
    Mix mx; mx.d = 7.0; mx.l = 9;
    Mix my = c_mix_swap(mx);
    println("mix: {{my.d}} {{my.l}}");
    println("spill: {{c_spill(1, 2, 3, 4, 5, rc)}}");

    float t = 0.0;
    int i = 0;
    while (i < 5) { t = t + sqrt(cast<float>(i)); i++; }
    println("sum sqrt = {{t}}");
    return 0;
}
