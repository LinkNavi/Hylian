include { std.io, }

struct Vec2 { float32 x; float32 y; }

static int helper(int a, int b) { return a + b; }

int apply(usize f, int x) { return f(x, 1); }

int main() {
    usize fp = addrof_fn(helper);
    int r = fp(3, 4);
    int arr[2];
    arr[0] = 1;
    arr[r - 7] += 2;
    return apply(fp, r);
}
