include { std.io, }

T max<T>(T a, T b) { if (a > b) { return a; } return b; }

T first<T>(array<T> xs) { return xs[0]; }

struct Pair<A, B> {
    A first;
    B second;
}

class Box<T> {
    T value;
    int hits;
    Box(T v) { value = v; hits = 0; }
    T get() { hits = hits + 1; return value; }
    void set(T v) { value = v; }
}

Pair<A, B> make_pair<A, B>(A a, B b) { return Pair<A, B> { first: a, second: b }; }

float avg<T>(T a, T b) { return (a + b) / 2.0; }

int main() {
    println("max int: {{max(3, 9)}}");
    println("max explicit: {{max<int>(12, 4)}}");
    println("max float: {{max(2.5, 1.5)}}");
    array<int> xs = [5, 6, 7];
    println("first: {{first(xs)}}");

    Box<int> b = new Box<int>(41);
    b.set(b.get() + 1);
    println("box: {{b.get()}} hits {{b.hits}}");

    Box<str> s = new Box<str>("hello");
    println("box str: {{s.get()}}");

    Pair<int, str> p = make_pair(7, "seven");
    println("pair: {{p.first}} {{p.second}}");
    Pair<int, float> q = Pair<int, float> { first: 1, second: 0.5 };
    println("q: {{q.first}} {{q.second}}");
    println("avg {{avg(1.0, 2.0)}}");
    return more();
}

class Stack<T> {
    array<T> items;
    Stack() { items = []; }
    void push(T v) { items.push(v); }
    T pop() { return items.pop(); }
    int size() { return items.len; }
}

int sz<T>() { return sizeof(T); }

int more() {
    Stack<int> st = new Stack<int>();
    st.push(1); st.push(2); st.push(3);
    println("pop {{st.pop()}} size {{st.size()}}");
    // '<' and '>' that are comparisons, not generic brackets
    int i = 0; int n = 3; int total = 0;
    while (i<n) { total = total + i; i = i + 1; }
    bool lt = i<n;
    bool gt = n>i;
    println("cmp {{total}} {{lt}} {{gt}}");
    array<Pair<int,int> > ps = [];
    println("sz {{sz<int>()}} {{sz<Pair<int,int> >()}}");
    Stack<float> sf = new Stack<float>();
    sf.push(1.5);
    println("float stack {{sf.pop()}}");
    Box<Box<int> > bb = new Box<Box<int> >(new Box<int>(5));
    println("nested box {{bb.get().get()}}");
    bx := new Box(41);
    Box<str> by = new Box("inferred");
    println("inferred {{bx.get()}} {{by.get()}}");
    FloatBag fb = new FloatBag();
    fb.put(3.25);
    float fv = fb.at(0);
    println("float method {{fv}}");
    return 0;
}

// a float-returning *method* hands its result back in xmm0
class FloatBag {
    array<float> vals;
    FloatBag() { vals = []; }
    void put(float v) { vals.push(v); }
    float at(int i) { return vals[i]; }
}
