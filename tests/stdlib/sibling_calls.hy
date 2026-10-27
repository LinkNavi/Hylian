include { std.io, }
class Counter {
    int n;
    Counter() { n = 0; }
    void bump() { n = n + 1; }
    int twice() { bump(); bump(); return get(); }
    int get() { return n; }
    float half() { return 2.5; }
    float h2() { return half() * 2.0; }
}
int main() {
    Counter c = new Counter();
    println("{{c.twice()}}");
    float h = c.h2();
    println("{{h}}");
    return 0;
}
