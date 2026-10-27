include { std.io, }

T max<T>(T a, T b) { if (a > b) { return a; } return b; }

struct Pair<A, B> { A first; B second; }

class Box<T> {
    T value;
    Box(T v) { value = v; }
    T get() { return value; }
}

int main() {
    Box<int> b = new Box<int>(41);
    Box<Box<int> > nested = new Box<Box<int> >(new Box<int>(1));
    Pair<int, str> p;
    println("{{max(1, 2)}} {{b.get()}}");
    int i = 0; int n = 3;
    bool lt = i<n;
    return 0;
}
