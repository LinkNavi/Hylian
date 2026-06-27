// runtime/platform/kernel.c
// Hylian freestanding kernel platform
// No libc, no syscalls — runs bare metal (x86-64, BIOS/Multiboot)

// Freestanding integer types — no libc, no stdint.h
typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;
typedef signed char        int8_t;
typedef signed short       int16_t;
typedef signed int         int32_t;
typedef signed long long   int64_t;

// ── VGA text mode ─────────────────────────────────────────────────────────────

#define VGA_BUFFER        ((volatile uint16_t*)0xB8000)
#define VGA_WIDTH         80
#define VGA_HEIGHT        25
#define VGA_DEFAULT_COLOR 0x07   // light grey on black

static int     vga_row   = 0;
static int     vga_col   = 0;
static uint8_t vga_color = VGA_DEFAULT_COLOR;

static void vga_scroll(void) {
    for (int r = 0; r < VGA_HEIGHT - 1; r++)
        for (int c = 0; c < VGA_WIDTH; c++)
            VGA_BUFFER[r * VGA_WIDTH + c] = VGA_BUFFER[(r + 1) * VGA_WIDTH + c];
    for (int c = 0; c < VGA_WIDTH; c++)
        VGA_BUFFER[(VGA_HEIGHT - 1) * VGA_WIDTH + c] =
            (uint16_t)(' ' | ((uint16_t)vga_color << 8));
    vga_row = VGA_HEIGHT - 1;
}

static void vga_putchar(char ch) {
    if (ch == '\n') {
        vga_col = 0;
        vga_row++;
        if (vga_row >= VGA_HEIGHT) vga_scroll();
        return;
    }
    if (ch == '\r') {
        vga_col = 0;
        return;
    }
    if (vga_col >= VGA_WIDTH) {
        vga_col = 0;
        vga_row++;
        if (vga_row >= VGA_HEIGHT) vga_scroll();
    }
    VGA_BUFFER[vga_row * VGA_WIDTH + vga_col] =
        (uint16_t)((uint8_t)ch | ((uint16_t)vga_color << 8));
    vga_col++;
}

// ── CPU control ───────────────────────────────────────────────────────────────

void hy_halt(void) {
    while (1) {
        __asm__ volatile ("hlt");
    }
}

void hy_exit(int code) {
    (void)code;
    hy_halt();
}

// ── I/O ───────────────────────────────────────────────────────────────────────

long hy_write(int fd, const void *buf, unsigned long len) {
    (void)fd;
    const char *s = (const char *)buf;
    for (unsigned long i = 0; i < len; i++)
        vga_putchar(s[i]);
    return (long)len;
}

long hy_read(int fd, void *buf, unsigned long len) {
    (void)fd; (void)buf; (void)len;
    return -1;   // no keyboard driver in minimal platform
}

int hy_open(const char *path, int flags, int mode) {
    (void)path; (void)flags; (void)mode;
    return -1;
}

void hy_close(int fd) {
    (void)fd;
}

// ── Bump allocator (1 MiB static heap) ───────────────────────────────────────

#define HEAP_SIZE (1024UL * 1024UL)
static char          heap[HEAP_SIZE];
static unsigned long heap_top = 0;

void *hy_alloc(unsigned long bytes) {
    unsigned long aligned = (heap_top + 15UL) & ~15UL;
    if (aligned + bytes > HEAP_SIZE) return (void *)0;
    heap_top = aligned + bytes;
    return (void *)(heap + aligned);
}

void hy_free(void *ptr, unsigned long bytes) {
    (void)ptr; (void)bytes;
    // bump allocator: free is a no-op
}

// ── Hylian runtime ABI ────────────────────────────────────────────────────────

void hylian_print(const char *buf, unsigned long len) {
    hy_write(0, buf, len);
}

void hylian_println(const char *buf, unsigned long len) {
    hy_write(0, buf, len);
    vga_putchar('\n');
}

long hylian_int_to_str(long val, char *buf, long buflen) {
    if (buflen <= 0) return 0;
    if (val == 0) {
        if (buflen < 2) return 0;
        buf[0] = '0';
        buf[1] = '\0';
        return 1;
    }
    int neg = 0;
    if (val < 0) { neg = 1; val = -val; }
    char tmp[32];
    int  pos = 0;
    while (val > 0) {
        tmp[pos++] = (char)('0' + (val % 10));
        val /= 10;
    }
    if (neg) tmp[pos++] = '-';
    int len = pos;
    if (len + 1 > buflen) return 0;
    for (int i = 0; i < len; i++)
        buf[i] = tmp[len - 1 - i];
    buf[len] = '\0';
    return len;
}

// ── Kernel-specific VGA helpers (callable via std.kernel) ─────────────────────

void hylian_vga_set_color(int color) {
    vga_color = (uint8_t)(color & 0xFF);
}

void hylian_vga_clear(void) {
    vga_row = 0;
    vga_col = 0;
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
        VGA_BUFFER[i] = (uint16_t)(' ' | ((uint16_t)vga_color << 8));
}

// ── Port I/O ──────────────────────────────────────────────────────────────────

void hylian_outb(int port, int value) {
    __asm__ volatile (
        "outb %b1, %w0"
        :
        : "Nd"((uint16_t)port), "a"((uint8_t)value)
    );
}

int hylian_inb(int port) {
    uint8_t val;
    __asm__ volatile (
        "inb %w1, %b0"
        : "=a"(val)
        : "Nd"((uint16_t)port)
    );
    return (int)val;
}

// ── Halt wrapper ──────────────────────────────────────────────────────────────

void hylian_halt(void) {
    hy_halt();
}

// ── C runtime stubs ───────────────────────────────────────────────────────────
// These satisfy any lingering references from generated code or string interp.

unsigned long strlen(const char *s) {
    unsigned long n = 0;
    while (s[n]) n++;
    return n;
}

void *malloc(unsigned long size) {
    return hy_alloc(size);
}

void *realloc(void *ptr, unsigned long new_size) {
    void *n = hy_alloc(new_size);
    if (ptr && n) {
        char *src = (char *)ptr;
        char *dst = (char *)n;
        // We don't know the old allocation size, so copy up to new_size.
        // Callers should not rely on stale bytes beyond the old length.
        for (unsigned long i = 0; i < new_size; i++)
            dst[i] = src[i];
    }
    return n;
}

void free(void *ptr) {
    (void)ptr; // bump allocator: no-op
}
long hy_getcwd(char *buf, unsigned long size) { (void)buf;(void)size; return -1; }
long hy_getenv(const char *name, char *buf, unsigned long buf_len) { (void)name;(void)buf;(void)buf_len; return -1; }
