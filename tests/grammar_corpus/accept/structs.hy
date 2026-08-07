// struct: a by-value aggregate with methods. All three grammars must accept
// fields AND functions inside the body, plus the packed/public modifiers.
struct Point {
    int x;
    int y;

    int sum() {
        return x + y;
    }

    void scale(int k) {
        x = x * k;
        y = y * k;
    }

    int dot(Point other) {
        return x * other.x + y * other.y;
    }
}

packed struct Header {
    uint8  kind;
    uint16 len;
    uint32 crc;
}

public struct Exported {
    int value;
    int get() { return value; }
}

struct Nested {
    Point a;
    Point b;
    int dx() { return b.x - a.x; }
}

struct Empty {
}
