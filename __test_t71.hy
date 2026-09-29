include {
    std.io,
}
public class Counter {
    int count;
    Counter() {
        count = 0;
    }
    void increment() {
        count = count + 1;
    }
    int get() {
        return count;
    }
}
Error? main() {
    Counter c = new Counter();
    c.increment();
    c.increment();
    c.increment();
    println(c.get());
    return nil;
}