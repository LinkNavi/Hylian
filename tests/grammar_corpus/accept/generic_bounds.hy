include { std.io, }

T biggest<T: Ord>(T a, T b) { if (a > b) { return a; } return b; }

struct Counter {
    int n;
    int next() { n = n + 1; return n; }
}

class Holder<T: Counter> {
    T inner;
    Holder(T c) { inner = c; }
    int bump() { return inner.next(); }
}

int main() {
    println("{{biggest(3, 9)}} {{biggest<int>(8, 2)}} {{biggest(1.5, 0.5)}}");
    Counter c; c.n = 4;
    Holder<Counter> h = new Holder<Counter>(c);
    println("{{h.bump()}}");
    return 0;
}
