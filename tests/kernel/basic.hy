include { std.kernel, }
struct P { int a; int b; }
static int digits[24];
static int arr[100];
void putc(int c) { outb(0x3F8, c); }
void putint(int n) {
    if (n < 0) { putc(45); n = 0 - n; }
    int len = 0;
    if (n == 0) { putc(48); return; }
    while (n > 0) { digits[len] = n % 10; len++; n = n / 10; }
    while (len > 0) { len--; putc(48 + digits[len]); }
}
void nl() { putc(10); }
int fib(int n) { if (n < 2) { return n; } return fib(n - 1) + fib(n - 2); }
int dotp(P p) { return p.a * 3 + p.b; }
int main() {
    putint(fib(20)); nl();
    for (int i = 0; i < 100; i++) { arr[i] = i * i; }
    int s = 0;
    for (int i = 0; i < 100; i++) { s += arr[i]; }
    putint(s); nl();
    P p; p.a = 5; p.b = 7;
    putint(dotp(p)); nl();
    putint(1000000007 % 97 + 12345678 / 13); nl();
    int f = 3;
    for (int i = 0; i < 5; i++) { f = f * 2; }
    putint(f); nl();
    putc(79); putc(75); nl();
    while (true) { halt(); }
    return 0;
}
