// runtime/platform/limine.c
// Hylian Limine bootloader platform
// Provides framebuffer/serial output for kernels loaded by Limine.

// Freestanding types
typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;
typedef signed long long   int64_t;
typedef unsigned long long uintptr_t;

// ── Limine protocol requests ─────────────────────────────────────────────────

#define LIMINE_COMMON_MAGIC0 0xc7b1dd30df4c8b88ULL
#define LIMINE_COMMON_MAGIC1 0x0a82e883a194f07bULL

struct limine_video_mode {
    uint64_t pitch;
    uint64_t width;
    uint64_t height;
    uint16_t bpp;
    uint8_t  memory_model;
    uint8_t  red_mask_size;
    uint8_t  red_mask_shift;
    uint8_t  green_mask_size;
    uint8_t  green_mask_shift;
    uint8_t  blue_mask_size;
    uint8_t  blue_mask_shift;
};

struct limine_framebuffer {
    void    *address;
    uint64_t width;
    uint64_t height;
    uint64_t pitch;
    uint16_t bpp;
    uint8_t  memory_model;
    uint8_t  red_mask_size;
    uint8_t  red_mask_shift;
    uint8_t  green_mask_size;
    uint8_t  green_mask_shift;
    uint8_t  blue_mask_size;
    uint8_t  blue_mask_shift;
    uint8_t  unused[7];
    uint64_t edid_size;
    void    *edid;
    uint64_t mode_count;
    struct limine_video_mode **modes;
};

struct limine_framebuffer_response {
    uint64_t revision;
    uint64_t framebuffer_count;
    struct limine_framebuffer **framebuffers;
};

struct limine_framebuffer_request {
    uint64_t id[4];
    uint64_t revision;
    struct limine_framebuffer_response *response;
};

// ── Memmap request ─────────────────────────────────────────────────────────

#define LIMINE_MEMMAP_USABLE                 0
#define LIMINE_MEMMAP_RESERVED               1
#define LIMINE_MEMMAP_ACPI_RECLAIMABLE       2
#define LIMINE_MEMMAP_ACPI_NVS               3
#define LIMINE_MEMMAP_BAD_MEMORY             4
#define LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE 5
#define LIMINE_MEMMAP_KERNEL_AND_MODULES     6
#define LIMINE_MEMMAP_FRAMEBUFFER            7

struct limine_memmap_entry {
    uint64_t base;
    uint64_t length;
    uint64_t type;
};

struct limine_memmap_response {
    uint64_t revision;
    uint64_t entry_count;
    struct limine_memmap_entry **entries;
};

struct limine_memmap_request {
    uint64_t id[4];
    uint64_t revision;
    struct limine_memmap_response *response;
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memmap_request = {
    .id = {
        LIMINE_COMMON_MAGIC0, LIMINE_COMMON_MAGIC1,
        0x67cf3d9d378a806fULL, 0xe304acdfc50c3c62ULL,
    },
    .revision = 0,
    .response = (void *)0,
};

// ── HHDM request ─────────────────────────────────────────────────────────────

struct limine_hhdm_response {
    uint64_t revision;
    uint64_t offset;
};

struct limine_hhdm_request {
    uint64_t id[4];
    uint64_t revision;
    struct limine_hhdm_response *response;
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = {
        LIMINE_COMMON_MAGIC0, LIMINE_COMMON_MAGIC1,
        0x48dcf1cb8ad2b852ULL, 0x63984e959a98244bULL,
    },
    .revision = 0,
    .response = (void *)0,
};

__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t limine_requests_start_marker[4] = {
    0xf6b8f4b39de7d1aeULL, 0xfab91a6940fcb9cfULL,
    0x785c6ed015d3e316ULL, 0x181e920a7852b9d9ULL,
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = {
        LIMINE_COMMON_MAGIC0, LIMINE_COMMON_MAGIC1,
        0x9d5827dcd881dd75ULL, 0xa3148604f6fab11bULL,
    },
    .revision = 0,
    .response = (void *)0,
};

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t limine_requests_end_marker[2] = {
    0xadc0e0531bb10d03ULL, 0x9572709f31764c62ULL,
};

// ── VGA text fallback ────────────────────────────────────────────────────────

#define VGA_BUFFER        ((volatile uint16_t*)0xB8000)
#define VGA_WIDTH         80
#define VGA_HEIGHT        25
#define VGA_DEFAULT_COLOR 0x07

static int     text_row   = 0;
static int     text_col   = 0;
static uint8_t text_color = VGA_DEFAULT_COLOR;

static const uint32_t vga_rgb[16] = {
    0x000000, 0x0000AA, 0x00AA00, 0x00AAAA,
    0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
    0x555555, 0x5555FF, 0x55FF55, 0x55FFFF,
    0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF,
};

static uint32_t fg_rgb(void) { return vga_rgb[text_color & 0x0F]; }
static uint32_t bg_rgb(void) { return vga_rgb[(text_color >> 4) & 0x0F]; }

static void vga_scroll(void) {
    for (int r = 0; r < VGA_HEIGHT - 1; r++)
        for (int c = 0; c < VGA_WIDTH; c++)
            VGA_BUFFER[r * VGA_WIDTH + c] = VGA_BUFFER[(r + 1) * VGA_WIDTH + c];
    for (int c = 0; c < VGA_WIDTH; c++)
        VGA_BUFFER[(VGA_HEIGHT - 1) * VGA_WIDTH + c] =
            (uint16_t)(' ' | ((uint16_t)text_color << 8));
    text_row = VGA_HEIGHT - 1;
}

static void vga_putchar(char ch) {
    if (ch == '\n') {
        text_col = 0;
        text_row++;
        if (text_row >= VGA_HEIGHT) vga_scroll();
        return;
    }
    if (ch == '\r') { text_col = 0; return; }
    if (text_col >= VGA_WIDTH) {
        text_col = 0;
        text_row++;
        if (text_row >= VGA_HEIGHT) vga_scroll();
    }
    VGA_BUFFER[text_row * VGA_WIDTH + text_col] =
        (uint16_t)((uint8_t)ch | ((uint16_t)text_color << 8));
    text_col++;
}

// ── COM1 serial port ─────────────────────────────────────────────────────────

#define COM1 0x3F8

static void serial_init(void) {
    __asm__ volatile ("outb %b1,%w0" :: "Nd"((uint16_t)(COM1+1)), "a"((uint8_t)0x00));
    __asm__ volatile ("outb %b1,%w0" :: "Nd"((uint16_t)(COM1+3)), "a"((uint8_t)0x80));
    __asm__ volatile ("outb %b1,%w0" :: "Nd"((uint16_t)(COM1+0)), "a"((uint8_t)0x03));
    __asm__ volatile ("outb %b1,%w0" :: "Nd"((uint16_t)(COM1+1)), "a"((uint8_t)0x00));
    __asm__ volatile ("outb %b1,%w0" :: "Nd"((uint16_t)(COM1+3)), "a"((uint8_t)0x03));
    __asm__ volatile ("outb %b1,%w0" :: "Nd"((uint16_t)(COM1+2)), "a"((uint8_t)0xC7));
}

static int serial_ready = 0;

static void serial_putchar(char ch) {
    if (!serial_ready) {
        serial_init();
        serial_ready = 1;
    }
    uint8_t lsr;
    do {
        __asm__ volatile ("inb %w1,%b0" : "=a"(lsr) : "Nd"((uint16_t)(COM1+5)));
    } while (!(lsr & 0x20));
    if (ch == '\n') {
        __asm__ volatile ("outb %b1,%w0" :: "Nd"((uint16_t)COM1), "a"((uint8_t)'\r'));
        do {
            __asm__ volatile ("inb %w1,%b0" : "=a"(lsr) : "Nd"((uint16_t)(COM1+5)));
        } while (!(lsr & 0x20));
    }
    __asm__ volatile ("outb %b1,%w0" :: "Nd"((uint16_t)COM1), "a"((uint8_t)ch));
}

// ── Framebuffer text renderer ────────────────────────────────────────────────

#define FONT_W 5
#define FONT_H 7
#define FONT_SCALE 2
#define CELL_W ((FONT_W + 1) * FONT_SCALE)
#define CELL_H ((FONT_H + 1) * FONT_SCALE)

static struct limine_framebuffer *fb_get(void) {
    struct limine_framebuffer_response *r = framebuffer_request.response;
    if (!r || r->framebuffer_count == 0 || !r->framebuffers) return (void *)0;
    struct limine_framebuffer *fb = r->framebuffers[0];
    if (!fb || !fb->address) return (void *)0;
    if (fb->bpp != 32 && fb->bpp != 24) return (void *)0;
    return fb;
}

static void fb_put_pixel(struct limine_framebuffer *fb, uint64_t x, uint64_t y, uint32_t rgb) {
    if (x >= fb->width || y >= fb->height) return;
    uint8_t *p = (uint8_t *)fb->address + y * fb->pitch + x * (fb->bpp / 8);
    if (fb->bpp == 32) {
        *(uint32_t *)p = rgb;
    } else if (fb->bpp == 24) {
        p[0] = (uint8_t)(rgb & 0xFF);
        p[1] = (uint8_t)((rgb >> 8) & 0xFF);
        p[2] = (uint8_t)((rgb >> 16) & 0xFF);
    }
}

static void fb_fill_rect(struct limine_framebuffer *fb, uint64_t x, uint64_t y,
                         uint64_t w, uint64_t h, uint32_t rgb) {
    for (uint64_t yy = 0; yy < h; yy++)
        for (uint64_t xx = 0; xx < w; xx++)
            fb_put_pixel(fb, x + xx, y + yy, rgb);
}

static const uint8_t font_digits[10][7] = {
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},
    {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
    {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F},
    {0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E},
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},
    {0x1F,0x10,0x10,0x1E,0x01,0x01,0x1E},
    {0x0E,0x10,0x10,0x1E,0x11,0x11,0x0E},
    {0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},
    {0x0E,0x11,0x11,0x0F,0x01,0x01,0x0E},
};

static const uint8_t font_alpha[26][7] = {
    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, // A
    {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, // B
    {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, // C
    {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}, // D
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, // E
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}, // F
    {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}, // G
    {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}, // H
    {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}, // I
    {0x01,0x01,0x01,0x01,0x11,0x11,0x0E}, // J
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, // K
    {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}, // L
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, // M
    {0x11,0x19,0x15,0x13,0x11,0x11,0x11}, // N
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, // O
    {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, // P
    {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, // Q
    {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}, // R
    {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}, // S
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, // T
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, // U
    {0x11,0x11,0x11,0x11,0x0A,0x0A,0x04}, // V
    {0x11,0x11,0x11,0x15,0x15,0x15,0x0A}, // W
    {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}, // X
    {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}, // Y
    {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}, // Z
};

static uint8_t glyph_row(char ch, int row) {
    if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
    if (ch >= 'A' && ch <= 'Z') return font_alpha[ch - 'A'][row];
    if (ch >= '0' && ch <= '9') return font_digits[ch - '0'][row];
    switch (ch) {
        case '.': return row == 6 ? 0x04 : 0x00;
        case ',': return row >= 5 ? (row == 5 ? 0x04 : 0x08) : 0x00;
        case ':': return (row == 2 || row == 5) ? 0x04 : 0x00;
        case ';': return row == 2 ? 0x04 : (row >= 5 ? (row == 5 ? 0x04 : 0x08) : 0x00);
        case '!': return row < 5 ? 0x04 : (row == 6 ? 0x04 : 0x00);
        case '?': return (uint8_t[]){0x0E,0x11,0x01,0x02,0x04,0x00,0x04}[row];
        case '-': return row == 3 ? 0x1F : 0x00;
        case '_': return row == 6 ? 0x1F : 0x00;
        case '/': return (uint8_t[]){0x01,0x02,0x02,0x04,0x08,0x08,0x10}[row];
        case '\\': return (uint8_t[]){0x10,0x08,0x08,0x04,0x02,0x02,0x01}[row];
        case '(': return (uint8_t[]){0x02,0x04,0x08,0x08,0x08,0x04,0x02}[row];
        case ')': return (uint8_t[]){0x08,0x04,0x02,0x02,0x02,0x04,0x08}[row];
        case '[': return (uint8_t[]){0x0E,0x08,0x08,0x08,0x08,0x08,0x0E}[row];
        case ']': return (uint8_t[]){0x0E,0x02,0x02,0x02,0x02,0x02,0x0E}[row];
        case '+': return row == 3 ? 0x0E : (row == 2 || row == 4 ? 0x04 : 0x00);
        case '=': return (row == 2 || row == 4) ? 0x1F : 0x00;
        case '*': return (row == 1 || row == 5) ? 0x15 : (row == 2 || row == 4 ? 0x0E : (row == 3 ? 0x04 : 0x00));
        case '<': return (uint8_t[]){0x01,0x02,0x04,0x08,0x04,0x02,0x01}[row];
        case '>': return (uint8_t[]){0x10,0x08,0x04,0x02,0x04,0x08,0x10}[row];
        default: return 0x00;
    }
}

static void fb_draw_char(struct limine_framebuffer *fb, char ch, uint64_t x, uint64_t y) {
    uint32_t fg = fg_rgb();
    uint32_t bg = bg_rgb();
    fb_fill_rect(fb, x, y, CELL_W, CELL_H, bg);
    if (ch == ' ') return;
    for (int row = 0; row < FONT_H; row++) {
        uint8_t bits = glyph_row(ch, row);
        for (int col = 0; col < FONT_W; col++) {
            if (bits & (1u << (FONT_W - 1 - col))) {
                fb_fill_rect(fb,
                    x + (uint64_t)col * FONT_SCALE,
                    y + (uint64_t)row * FONT_SCALE,
                    FONT_SCALE, FONT_SCALE, fg);
            }
        }
    }
}

static void fb_clear_screen(struct limine_framebuffer *fb) {
    fb_fill_rect(fb, 0, 0, fb->width, fb->height, bg_rgb());
    text_row = 0;
    text_col = 0;
}

static void fb_putchar(char ch) {
    struct limine_framebuffer *fb = fb_get();
    if (!fb) { vga_putchar(ch); return; }

    int cols = (int)(fb->width / CELL_W);
    int rows = (int)(fb->height / CELL_H);
    if (cols <= 0 || rows <= 0) return;

    if (ch == '\n') {
        text_col = 0;
        text_row++;
        if (text_row >= rows) fb_clear_screen(fb);
        return;
    }
    if (ch == '\r') { text_col = 0; return; }
    if (text_col >= cols) {
        text_col = 0;
        text_row++;
        if (text_row >= rows) fb_clear_screen(fb);
    }

    fb_draw_char(fb, ch, (uint64_t)text_col * CELL_W, (uint64_t)text_row * CELL_H);
    text_col++;
}

static void display_putchar(char ch) {
    fb_putchar(ch);
}

// ── CPU ───────────────────────────────────────────────────────────────────────

void hy_halt(void) {
    while (1) __asm__ volatile ("hlt");
}

void hy_exit(int code) {
    (void)code;
    hy_halt();
}

// ── I/O ───────────────────────────────────────────────────────────────────────

long hy_write(int fd, const void *buf, unsigned long len) {
    (void)fd;
    for (unsigned long i = 0; i < len; i++) {
        char ch = ((const char*)buf)[i];
        display_putchar(ch);
        serial_putchar(ch);
    }
    return (long)len;
}

long hy_read(int fd, void *buf, unsigned long len) {
    (void)fd; (void)buf; (void)len; return -1;
}

int  hy_open(const char *p, int f, int m) { (void)p; (void)f; (void)m; return -1; }
void hy_close(int fd) { (void)fd; }

// ── Bump allocator ────────────────────────────────────────────────────────────

#define HEAP_SIZE (4UL * 1024UL * 1024UL)
static char          heap[HEAP_SIZE];
static unsigned long heap_top = 0;

void *hy_alloc(unsigned long bytes) {
    unsigned long a = (heap_top + 15UL) & ~15UL;
    if (a + bytes > HEAP_SIZE) return (void*)0;
    heap_top = a + bytes;
    return heap + a;
}

void hy_free(void *p, unsigned long b) { (void)p; (void)b; }

// ── Hylian runtime ABI ────────────────────────────────────────────────────────

void hylian_print(const char *buf, unsigned long len) {
    hy_write(0, buf, len);
}

void hylian_println(const char *buf, unsigned long len) {
    hy_write(0, buf, len);
    display_putchar('\n');
    serial_putchar('\n');
}

long hylian_int_to_str(long val, char *buf, long buflen) {
    if (buflen <= 0) return 0;
    if (val == 0) { buf[0] = '0'; buf[1] = '\0'; return 1; }
    int neg = val < 0;
    if (neg) val = -val;
    char tmp[32];
    int pos = 0;
    while (val > 0) { tmp[pos++] = (char)('0' + (val % 10)); val /= 10; }
    if (neg) tmp[pos++] = '-';
    int len = pos;
    if (len + 1 > buflen) return 0;
    for (int i = 0; i < len; i++) buf[i] = tmp[len - 1 - i];
    buf[len] = '\0';
    return len;
}

void hylian_vga_set_color(int c) { text_color = (uint8_t)(c & 0xFF); }

void hylian_vga_clear(void) {
    struct limine_framebuffer *fb = fb_get();
    if (fb) {
        fb_clear_screen(fb);
        return;
    }
    text_row = text_col = 0;
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
        VGA_BUFFER[i] = (uint16_t)(' ' | ((uint16_t)text_color << 8));
}

void hylian_outb(int port, int value) {
    __asm__ volatile ("outb %b1, %w0"
        :: "Nd"((uint16_t)port), "a"((uint8_t)value));
}

int hylian_inb(int port) {
    uint8_t v;
    __asm__ volatile ("inb %w1, %b0"
        : "=a"(v) : "Nd"((uint16_t)port));
    return v;
}

void hylian_halt(void) { hy_halt(); }

// ── Limine memmap / HHDM accessors (called from pmm.hy) ──────────────────────

uint64_t hylian_hhdm_offset(void) {
    if (!hhdm_request.response) return 0;
    return hhdm_request.response->offset;
}

// Returns the number of memmap entries, or 0 if unavailable.
uint64_t hylian_memmap_count(void) {
    if (!memmap_request.response) return 0;
    return memmap_request.response->entry_count;
}

// Returns the base address of memmap entry i.
uint64_t hylian_memmap_base(uint64_t i) {
    if (!memmap_request.response) return 0;
    return memmap_request.response->entries[i]->base;
}

// Returns the length in bytes of memmap entry i.
uint64_t hylian_memmap_len(uint64_t i) {
    if (!memmap_request.response) return 0;
    return memmap_request.response->entries[i]->length;
}

// Returns the type of memmap entry i (0 = usable).
uint64_t hylian_memmap_type(uint64_t i) {
    if (!memmap_request.response) return 0xFFFFFFFFFFFFFFFFULL;
    return memmap_request.response->entries[i]->type;
}

// ── C runtime stubs ───────────────────────────────────────────────────────────

unsigned long strlen(const char *s) {
    unsigned long n = 0;
    while (s[n]) n++;
    return n;
}

void *malloc(unsigned long sz) { return hy_alloc(sz); }

void *realloc(void *p, unsigned long sz) {
    void *n = hy_alloc(sz);
    if (p && n) {
        char *s = (char*)p, *d = (char*)n;
        for (unsigned long i = 0; i < sz; i++) d[i] = s[i];
    }
    return n;
}

void free(void *p) { (void)p; }

// Limine calls _start directly. The Hylian compiler generates the user's
// main() as _start for freestanding targets; this file provides runtime support.
