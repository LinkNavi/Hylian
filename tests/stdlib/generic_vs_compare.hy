include { std.io, }

T mx<T>(T a, T b) { if (a > b) { return a; } return b; }

int main() {
    int a = 3;
    int b = 5;
    int m = 1;
    println("call: {{mx<int>(a, b)}}");
    if (m<b) { println("lt: yes"); }
    if (a>b) { println("gt: yes"); } else { println("gt: no"); }
    return 0;
}
