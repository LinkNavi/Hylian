#include "platform.h"

static long sc3(long n, long a, long b, long c) {
    long r;
    __asm__ volatile("syscall"
        : "=a"(r) : "0"(n|0x2000000),"D"(a),"S"(b),"d"(c)
        : "rcx","r11","memory");
    return (r >> 63) ? -r : r;
}

static long sc6(long n,long a,long b,long c,long d,long e,long f) {
    register long r10 __asm__("r10")=d;
    register long r8  __asm__("r8") =e;
    register long r9  __asm__("r9") =f;
    long r;
    __asm__ volatile("syscall"
        : "=a"(r) : "0"(n|0x2000000),"D"(a),"S"(b),"d"(c),"r"(r10),"r"(r8),"r"(r9)
        : "rcx","r11","memory");
    return (r >> 63) ? -r : r;
}

void *hy_alloc(hy_size bytes) {
    // mmap = 197 on macOS
    long a = sc6(197, 0,(long)bytes,3,4098,-1,0);
    return a < 0 ? (void*)0 : (void*)a;
}
void hy_free(void *ptr, hy_size bytes) { sc3(73,(long)ptr,(long)bytes,0); }
hy_ssize hy_write(int fd,const void *buf,hy_size len) { return sc3(4,fd,(long)buf,(long)len); }
hy_ssize hy_read(int fd,void *buf,hy_size len)        { return sc3(3,fd,(long)buf,(long)len); }
void hy_exit(int code) { sc3(1,code,0,0); __builtin_unreachable(); }
int  hy_open(const char *path,int flags,int mode)     { return (int)sc3(5,(long)path,flags,mode); }
void hy_close(int fd)                                  { sc3(6,fd,0,0); }
hy_ssize hy_pread(int fd,void *buf,hy_size len,long off)        { return sc6(153,fd,(long)buf,(long)len,off,0,0); }
hy_ssize hy_pwrite(int fd,const void *buf,hy_size len,long off) { return sc6(154,fd,(long)buf,(long)len,off,0,0); }
long hy_fsize(int fd) {
    char buf[144]; long r=sc3(189,fd,(long)buf,0);
    if(r<0) return -1; long s; __builtin_memcpy(&s,buf+48,8); return s;
}
int hy_mkdir(const char *path,int mode) { return (int)sc3(136,(long)path,mode,0); }
