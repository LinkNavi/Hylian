include { std.kernel, }
static uint8 idt[4096];
static int ticks = 0;
static int inmask = 0;
void putc(int c) { outb(0x3F8, c); }
void putint(int n) {
    if (n == 0) { putc(48); return; }
    int d0 = 0; int len = 0; int digs = 0;
    int m = n; int div = 1;
    while (m >= 10) { m = m / 10; div = div * 10; }
    while (div > 0) { putc(48 + (n / div) % 10); div = div / 10; }
}
void nl() { putc(10); }
naked void timer_isr() {
    save_regs(1);
    ticks += 1;
    outb(0x20, 0x20);
    restore_regs(1);
    iret();
}
void set_gate(int v, usize fn, int sel) {
    int o = v * 16;
    idt[o] = fn & 255;
    idt[o + 1] = (fn >> 8) & 255;
    idt[o + 2] = sel & 255;
    idt[o + 3] = (sel >> 8) & 255;
    idt[o + 4] = 0;
    idt[o + 5] = 0x8E;
    idt[o + 6] = (fn >> 16) & 255;
    idt[o + 7] = (fn >> 24) & 255;
    idt[o + 8] = (fn >> 32) & 255;
    idt[o + 9] = (fn >> 40) & 255;
    idt[o + 10] = (fn >> 48) & 255;
    idt[o + 11] = (fn >> 56) & 255;
}
int main() {
    putc(66); putc(79); putc(79); putc(84); nl();
    set_gate(32, addrof_fn(timer_isr), 0x28);
    lidt(cast<uint64>(cast<usize>(&idt)), 4095);
    outb(0x20, 0x11); outb(0xA0, 0x11);
    outb(0x21, 0x20); outb(0xA1, 0x28);
    outb(0x21, 4); outb(0xA1, 2);
    outb(0x21, 1); outb(0xA1, 1);
    outb(0x21, 0xFE); outb(0xA1, 0xFF);
    outb(0x43, 0x36); outb(0x40, 0xA9); outb(0x40, 0x04);
    sti();
    while (ticks < 5) { hlt(); }
    cli();
    putc(84); putc(73); putc(67); putc(75); putc(83); putc(58); putint(ticks >= 5 ? 5 : 0); nl();
    while (true) { halt(); }
    return 0;
}
