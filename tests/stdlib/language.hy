include { std.io, std.string, }

/* block comments
   span lines */

typedef int myint;
typedef uint8 octet;

int counter = 10;            // plain top-level variable (no `static` needed)
float ratio = 2.5;

enum Level { Low, Mid = 5, High, Top }

struct Pt { int x; int y; }

int add(int a, int b = 5, int c = 100) { return a + b + c; }
str greet(str who, str greeting = "hello") { return greeting + " " + who; }
float half(float x) { return x / 2.0; }
uint8 bump(uint8 x) { return x + 200; }
int f7(int a, int b, int c, int d, int e, int g, int h) { return a + b*2 + c*3 + d*4 + e*5 + g*6 + h*7; }
float mix9(float a, int b, float c, int d, float e, int g, float h, int i, float j) {
    return a + b + c + d + e + g + h + i + j;
}

Error? check(str n) {
    if (length(n) == 0) { return Err("empty", 42); }
    if (length(n) > 5) { return Err("too long"); }
    return nil;
}

void note(str s) { println("  defer: {{s}}"); }

int deferred(int n) {
    defer note("outer");
    defer { note("block"); }
    if (n > 5) {
        defer note("big branch");
        return n * 2;
    }
    note("body");
    return n;
}

int main() {
    // for loops with ++ / -- / compound post-clauses
    int s = 0;
    for (int i = 0; i < 5; i++) { s += i; }
    for (int j = 10; j > 0; j -= 3) { s += 1000; }
    println("for: {{s}}");

    // ranges
    int r = 0;
    for (i in 0..4) { r += i; }
    for (k in 1..=3) { r += 100 * k; }
    println("range: {{r}}");

    // ternary, nested, with strings
    int a = 3;
    println("ternary: {{a > 2 ? 10 : 20}} {{a > 5 ? 1 : a > 2 ? 2 : 3}} {{a == 3 ? \"three\" : \"other\"}}");

    // do-while, with continue and break
    int n = 0; int k2 = 0;
    do { k2 += 1; if (k2 == 2) { continue; } if (k2 > 4) { break; } n += 1; } while (k2 < 10);
    int once = 0;
    do { once += 1; } while (false);
    println("do-while: {{n}} {{once}}");

    // literals
    println("literals: {{'a'}} {{'\n'}} {{'\x41'}} {{0xFF}} {{0b1010}} {{0o17}} {{1_000}}");

    // sizeof
    println("sizeof: {{sizeof(int)}} {{sizeof(uint8)}} {{sizeof(char)}} {{sizeof(Pt)}}");

    // compound assignment operators, including through fields / elements
    int c = 6;
    c *= 2; c -= 1; c /= 2; c %= 4; c <<= 2; c |= 1; c &= 7; c ^= 2; c >>= 1;
    array<int> v = [1, 2, 3];
    v[1] += 10;
    v[2] <<= 2;
    Pt p;
    p.x = 5;
    p.x *= 3;
    println("compound: {{c}} {{v[1]}} {{v[2]}} {{p.x}}");

    // nested generics close with >>
    array<array<int>> grid = [];
    array<int> row = [7, 8];
    grid.push(cast<int>(cast<usize>(row)));
    println("nested: {{grid.len}}");

    // globals, typedef, char, enum auto-increment
    counter += 5;
    myint m = 3;
    octet o = 7;
    char ch = 65;
    println("globals: {{counter}} {{m + o + ch}} {{Level.Low}} {{Level.Mid}} {{Level.High}} {{Level.Top}}");

    // default arguments
    println("defaults: {{add(1)}} {{add(1, 2)}} {{add(1, 2, 3)}} {{greet(\"bob\")}} {{greet(\"bob\", \"hi\")}}");

    // strings: indexing, byte assignment, +=, +
    str word = "hello";
    str buf = word + "";
    buf[0] = 'J';
    str acc = "a";
    acc += "b";
    acc += "c";
    println("strings: {{word[1]}} {{buf}} {{acc}}");

    // fixed-size local arrays
    int table[4];
    table[0] = 1; table[3] = 4; table[1] += 10;
    int dyn = 3;
    int table2[dyn * 2];
    println("arrays: {{table[0] + table[3] + table[1]}} {{table.len}} {{table2.len}}");

    // sized integers wrap like C
    uint8 u8 = 250; u8 = u8 + 10;
    uint8 u8b = 3; u8b = u8b - 5;
    uint16 u16 = 65535; u16 = u16 + 1;
    uint32 u32 = 4294967295; u32 = u32 + 1;
    int8 i8 = 127; i8 = i8 + 1;
    int32 i32 = 2147483647; i32 = i32 + 1;
    uint8 v8 = 100; int w = 0;
    while (w < 3) { v8 = bump(v8); w++; }
    uint8 inv = 0; inv = ~inv;
    println("wrap: {{u8}} {{u8b}} {{u16}} {{u32}} {{i8}} {{i32}} {{v8}} {{inv}}");

    // unsigned 64-bit arithmetic
    uint64 big = 18446744073709551615;
    uint64 halfbig = big / 2;
    println("u64: {{halfbig >> 60}} {{big % 10}} {{big > cast<uint64>(5) ? 1 : 0}}");

    // 32-bit FNV-1a, which relies on uint32 wraparound
    uint32 h = 2166136261;
    int q = 0;
    while (q < 5) { h = h ^ cast<uint32>(97 + q); h = h * 16777619; q++; }
    println("fnv: {{h}}");

    // floats: arithmetic, mixed int/float, functions, globals, printing
    float f = 1.5;
    double d = 2.25;
    float g = f * 2 + d;
    float32 f32 = 0.25;
    ratio = ratio * 2.0;
    println("floats: {{g}} {{half(5.0)}} {{ratio}} {{f32 * 4}} {{cast<int>(g * 100.0)}} {{3.14159}} {{1.5e20}} {{-2.5}}");
    println(g);
    println(1.0 / 3.0);

    // more than six arguments are passed on the stack
    println("manyargs: {{f7(1, 2, 3, 4, 5, 6, 7)}} {{mix9(1.5, 2, 3.5, 4, 5.5, 6, 7.5, 8, 9.5)}}");

    // NaN compares like IEEE 754; 0.0 division gives inf / nan instead of 0
    float zero = 0.0;
    float nan = zero / zero;
    float inf = 1.0 / zero;
    println("nan: {{nan == nan}} {{nan != nan}} {{nan < 1.0}} {{nan >= 1.0}} {{nan}} {{inf}} {{-inf}} {{1.0 < 2.0}}");

    // defer: reverse order, only if reached, runs on every return
    println("defer: {{deferred(10)}}");
    println("defer: {{deferred(1)}}");

    // errors with messages and codes
    Error? e1 = check("");
    Error? e2 = check("toolongname");
    Error? e3 = check("ok");
    println("errors: {{e1.message()}}/{{e1.code()}} {{e2.message()}}/{{e2.code()}} {{e3 == nil}}");

    // interpolation with expressions
    str who = "world";
    bool ok = a > 1;
    println("interp: {{who}} {{a * 2 + 1}} {{ok}} {{length(who)}}");
    return 0;
}
