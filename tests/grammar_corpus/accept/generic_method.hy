include { std.io, std.string, }

int twice(int x) { return x * 2; }
str show(int x) { return "<{{x}}>"; }

class Box<T> {
    T value;
    Box(T v) { value = v; }
    U map<U>(fn(T) -> U f) { return f(value); }
    T get() { return value; }
}

class Plain {
    int n;
    Plain(int v) { n = v; }
    T pick<T>(T a, T b) { if (n > 0) { return a; } return b; }
}

int main() {
    Box<int> b = new Box<int>(21);
    int d = b.map(addrof_fn(twice));
    str s = b.map<str>(addrof_fn(show));
    println("{{d}} {{s}}");
    Plain p = new Plain(1);
    int pi = p.pick(7, 9);
    str ps = p.pick("a", "b");
    println("{{pi}} {{ps}}");
    return 0;
}
