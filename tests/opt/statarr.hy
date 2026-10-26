include { std.io, }
static uint8 buf[16];
static int tab[5];
static uint32 w[4];
static float fl[3];
int sum_buf() { int s = 0; for (int i = 0; i < 16; i++) { s += buf[i]; } return s; }
Error? main() {
    for (int i = 0; i < 16; i++) { buf[i] = i * 20; }      // wraps at 8 bits
    for (int i = 0; i < 5; i++) { tab[i] = i * 1000000007; }
    w[0] = 4294967295; w[1] = w[0] + 2; w[2] = 7; w[3] = w[2] * 3;
    fl[0] = 1.5; fl[1] = fl[0] * 2.0; fl[2] = fl[1] + 0.25;
    println(sum_buf());
    println(buf[13]);
    println(tab[4]);
    println(w[0]); println(w[1]); println(w[3]);
    println(cast<int>(fl[2] * 100.0));
    tab[2] += 5; tab[3] <<= 1;
    println(tab[2] + tab[3]);
    return nil;
}
