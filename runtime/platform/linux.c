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
    unsigned long a = (unsigned long)sc6(9, 0, (long)bytes, 3, 34, -1, 0);
    /* mmap errors are small negative values (e.g. -ENOMEM = 0xffff...f4);
       valid mappings are below the 47-bit user address limit.  Treat any
       result >= the canonical hole as failure. */
    return a > (unsigned long)-4096UL ? (void*)0 : (void*)a;
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

long hy_getcwd(char *buf, hy_size size) {
    // getcwd syscall = 79
    return sc3(79, (long)buf, (long)size, 0);
}

/* getenv via /proc/self/environ — no libc needed.
   Scans null-separated KEY=VALUE entries for NAME=.
   Returns length written to buf on success, -1 on failure. */
long hy_getenv(const char *name, char *buf, hy_size buf_len) {
    int fd = hy_open("/proc/self/environ", 0, 0);
    if (fd < 0) return -1;

    char env[65536];
    long total = 0, n;
    while (total < (long)sizeof(env) - 1) {
        n = hy_read(fd, env + total, sizeof(env) - 1 - total);
        if (n <= 0) break;
        total += n;
    }
    hy_close(fd);
    env[total] = '\0';

    long name_len = 0;
    while (name[name_len]) name_len++;

    char *p = env;
    while (p < env + total) {
        /* check if this entry starts with NAME= */
        long i = 0;
        while (i < name_len && p[i] == name[i]) i++;
        if (i == name_len && p[i] == '=') {
            char *val = p + i + 1;
            long vl = 0;
            while (val[vl] && val[vl] != '\0') vl++;
            if (vl >= (long)buf_len) vl = (long)buf_len - 1;
            long j = 0;
            while (j < vl) { buf[j] = val[j]; j++; }
            buf[j] = '\0';
            return j;
        }
        /* skip to next entry (past null terminator) */
        while (p < env + total && *p) p++;
        p++;
    }
    return -1;
}
