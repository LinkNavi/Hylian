/*
 * Termina Platform Runtime
 * 
 * Provides minimal runtime support for the Termina VM.
 * This is a bare-metal/freestanding environment with syscalls via INT instruction.
 * 
 * Note: The actual syscall mechanism (INT instruction) is handled by the
 * Termina bytecode generator. This file provides C stubs that the compiler
 * can recognize and translate to appropriate bytecode.
 */

#include "platform.h"
#include <stdint.h>
#include <stddef.h>

/* ── Memory allocation ────────────────────────────────────────────────────── */

/* Simple bump allocator for Termina VM */
static uint8_t heap[1024 * 1024];  /* 1 MB heap */
static size_t heap_pos = 0;

void* malloc(size_t size) {
    if (heap_pos + size > sizeof(heap))
        return NULL;
    void* ptr = &heap[heap_pos];
    heap_pos += size;
    /* Align to 8 bytes */
    heap_pos = (heap_pos + 7) & ~7;
    return ptr;
}

void* realloc(void* ptr, size_t size) {
    /* Simple implementation: allocate new block and copy */
    if (!ptr)
        return malloc(size);
    
    void* new_ptr = malloc(size);
    if (!new_ptr)
        return NULL;
    
    /* Copy old data - this is simplified and assumes reasonable sizes */
    uint8_t* old = (uint8_t*)ptr;
    uint8_t* new = (uint8_t*)new_ptr;
    for (size_t i = 0; i < size; i++)
        new[i] = old[i];
    
    return new_ptr;
}

void free(void* ptr) {
    /* No-op in bump allocator */
    (void)ptr;
}

/* ── String functions ────────────────────────────────────────────────────── */

size_t strlen(const char* s) {
    size_t len = 0;
    while (s[len])
        len++;
    return len;
}

void* memcpy(void* dest, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    for (size_t i = 0; i < n; i++)
        d[i] = s[i];
    return dest;
}

void* memset(void* s, int c, size_t n) {
    uint8_t* p = (uint8_t*)s;
    for (size_t i = 0; i < n; i++)
        p[i] = (uint8_t)c;
    return s;
}

int strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

char* strcpy(char* dest, const char* src) {
    char* d = dest;
    while ((*d++ = *src++))
        ;
    return dest;
}

char* strcat(char* dest, const char* src) {
    char* d = dest;
    while (*d)
        d++;
    while ((*d++ = *src++))
        ;
    return dest;
}

/* ── I/O via syscalls ────────────────────────────────────────────────────── */

/*
 * Termina syscall: INT 0 = write(fd, buf, len)
 * The codegen will recognize these patterns and emit appropriate bytecode.
 */

void platform_write(int fd, const void* buf, size_t len) {
    /* This will be translated by codegen_termina to:
     *   MOVI r0, fd
     *   MOV  r1, buf_addr
     *   MOVI r2, len
     *   INT  0
     * For now, we can't actually call this from C, but the Hylian
     * compiler's println() intrinsic will generate the right bytecode.
     */
    (void)fd;
    (void)buf;
    (void)len;
}

void platform_exit(int code) {
    /* Will be translated to HLT instruction */
    (void)code;
    /* Unreachable in actual bytecode */
}

/* ── Arena allocator ─────────────────────────────────────────────────────── */

typedef struct Arena {
    uint8_t* base;
    size_t   size;
    size_t   used;
} Arena;

static Arena global_arena;

void arena_init(Arena* arena, size_t size) {
    arena->base = (uint8_t*)malloc(size);
    arena->size = arena->base ? size : 0;
    arena->used = 0;
}

void* arena_alloc(Arena* arena, size_t size) {
    if (!arena->base)
        return NULL;
    
    /* Align to 8 bytes */
    size_t aligned = (size + 7) & ~7;
    
    if (arena->used + aligned > arena->size)
        return NULL;
    
    void* ptr = arena->base + arena->used;
    arena->used += aligned;
    return ptr;
}

void arena_free(Arena* arena) {
    if (arena->base) {
        free(arena->base);
        arena->base = NULL;
        arena->size = 0;
        arena->used = 0;
    }
}

/* Global arena initialization (called by generated code) */
Arena* __get_global_arena(void) {
    if (!global_arena.base) {
        arena_init(&global_arena, 512 * 1024);  /* 512 KB default */
    }
    return &global_arena;
}

/* ── Print helpers ──────────────────────────────────────────────────────── */

void print_int(int64_t val) {
    char buf[32];
    int i = 0;
    int is_neg = 0;
    
    if (val == 0) {
        platform_write(1, "0", 1);
        return;
    }
    
    if (val < 0) {
        is_neg = 1;
        val = -val;
    }
    
    while (val > 0) {
        buf[i++] = '0' + (val % 10);
        val /= 10;
    }
    
    if (is_neg)
        buf[i++] = '-';
    
    /* Reverse */
    for (int j = 0; j < i / 2; j++) {
        char tmp = buf[j];
        buf[j] = buf[i - j - 1];
        buf[i - j - 1] = tmp;
    }
    
    platform_write(1, buf, i);
}

void print_str(const char* s) {
    if (s)
        platform_write(1, s, strlen(s));
}

/* ── Termina-specific stubs ─────────────────────────────────────────────── */

/* These are stubs that won't actually be used in Termina bytecode,
 * but are needed for compatibility with the runtime interface.
 */

void* mmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset) {
    (void)addr; (void)length; (void)prot; (void)flags; (void)fd; (void)offset;
    return NULL;
}

int munmap(void* addr, size_t length) {
    (void)addr; (void)length;
    return -1;
}

/* ── Entry point ────────────────────────────────────────────────────────── */

/*
 * The Termina bytecode generator creates an entry point that:
 * 1. Initializes the arena
 * 2. Calls main()
 * 3. Calls arena_free() on the global arena
 * 4. Issues HLT instruction
 * 
 * No special _start code is needed here.
 */
long hy_getcwd(char *buf, unsigned long size) { (void)buf;(void)size; return -1; }
long hy_getenv(const char *name, char *buf, unsigned long buf_len) { (void)name;(void)buf;(void)buf_len; return -1; }
