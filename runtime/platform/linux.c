#include "platform.h"

static long sc3(long n, long a, long b, long c) {
    long r;
    __asm__ volatile("syscall"
        : "=a"(r) : "0"(n),"D"(a),"S"(b),"d"(c)
        : "rcx","r11","memory");
    return r;
}

static long sc6(long n, long a, long b, long c, long d, long e, long f) {
    register long r10 __asm__("r10") = d;
    register long r8  __asm__("r8")  = e;
    register long r9  __asm__("r9")  = f;
    long r;
    __asm__ volatile("syscall"
        : "=a"(r) : "0"(n),"D"(a),"S"(b),"d"(c),"r"(r10),"r"(r8),"r"(r9)
        : "rcx","r11","memory");
    return r;
}

void *hy_alloc(hy_size bytes) {
    long a = sc6(9, 0, (long)bytes, 3, 34, -1, 0);
    return a < 0 ? (void*)0 : (void*)a;
}

void hy_free(void *ptr, hy_size bytes) {
    sc3(11, (long)ptr, (long)bytes, 0);
}

hy_ssize hy_write(int fd, const void *buf, hy_size len) {
    return sc3(1, fd, (long)buf, (long)len);
}

hy_ssize hy_read(int fd, void *buf, hy_size len) {
    return sc3(0, fd, (long)buf, (long)len);
}

void hy_exit(int code) {
    sc3(60, code, 0, 0);
    __builtin_unreachable();
}

int hy_open(const char *path, int flags, int mode) {
    return (int)sc3(2, (long)path, flags, mode);
}

void hy_close(int fd) {
    sc3(3, fd, 0, 0);
}

hy_ssize hy_pread(int fd, void *buf, hy_size len, long offset) {
    register long r10 __asm__("r10") = offset;
    long r;
    __asm__ volatile("syscall"
        : "=a"(r) : "0"(17L),"D"((long)fd),"S"(buf),"d"((long)len),"r"(r10)
        : "rcx","r11","memory");
    return r;
}

hy_ssize hy_pwrite(int fd, const void *buf, hy_size len, long offset) {
    register long r10 __asm__("r10") = offset;
    long r;
    __asm__ volatile("syscall"
        : "=a"(r) : "0"(18L),"D"((long)fd),"S"(buf),"d"((long)len),"r"(r10)
        : "rcx","r11","memory");
    return r;
}

long hy_fsize(int fd) {
    // stat syscall (5), buf layout: size at offset 48
    char buf[144];
    long r = sc3(5, fd, (long)buf, 0);
    if (r < 0) return -1;
    long size;
    __builtin_memcpy(&size, buf + 48, 8);
    return size;
}

int hy_mkdir(const char *path, int mode) {
    return (int)sc3(83, (long)path, mode, 0);
}
