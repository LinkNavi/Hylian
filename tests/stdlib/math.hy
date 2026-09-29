include { std.io, std.math, std.assert, }
int main() {
    assert_eq(max(3, 9), 9, "max");
    assert_eq(min(3, 9), 3, "min");
    assert_eq(abs(-4), 4, "abs");
    assert_eq(clamp(15, 0, 10), 10, "clamp");
    assert_eq(ipow(3, 5), 243, "ipow");
    assert_eq(isqrt(1000000), 1000, "isqrt");
    assert_eq(isqrt(99), 9, "isqrt2");
    assert_eq(gcd(48, 18), 6, "gcd");
    assert_near(sqrt(2.0), 1.4142135623730951, 1.0e-12, "sqrt");
    assert_near(floor(-2.5), -3.0, 1.0e-12, "floor");
    assert_near(ceil(2.1), 3.0, 1.0e-12, "ceil");
    assert_near(round(2.5), 3.0, 1.0e-12, "round");
    assert_near(round(-2.5), -3.0, 1.0e-12, "roundneg");
    assert_near(sin(PI / 6.0), 0.5, 1.0e-12, "sin");
    assert_near(cos(PI), -1.0, 1.0e-12, "cos");
    assert_near(sin(100.0), -0.5063656411097588, 1.0e-10, "sin100");
    assert_near(exp(1.0), E, 1.0e-12, "exp");
    assert_near(exp(-3.5), 0.0301973834223185, 1.0e-12, "expneg");
    assert_near(ln(E), 1.0, 1.0e-12, "ln");
    assert_near(ln(1000.0), 6.907755278982137, 1.0e-12, "ln1000");
    assert_near(pow(2.0, 10.0), 1024.0, 1.0e-9, "pow");
    assert_near(pow(2.0, 0.5), SQRT2, 1.0e-12, "powfrac");
    assert_near(atan(1.0), PI / 4.0, 1.0e-12, "atan");
    assert_near(atan2(1.0, -1.0), 3.0 * PI / 4.0, 1.0e-12, "atan2");
    assert_near(log10(1000.0), 3.0, 1.0e-12, "log10");
    println("math ok");
    println("{{sqrt(2.0)}} {{PI}} {{sin(1.0)}}");
    return 0;
}
