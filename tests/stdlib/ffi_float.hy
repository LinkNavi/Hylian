include { std.io, vendors.cmath, }

// Called from C (ffi_float.c): mixed double / int / double / float32 arguments.
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

    float t = 0.0;
    int i = 0;
    while (i < 5) { t = t + sqrt(cast<float>(i)); i++; }
    println("sum sqrt = {{t}}");
    return 0;
}
