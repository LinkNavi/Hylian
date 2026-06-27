#ifdef _WIN32
#include "platform.h"

__declspec(dllimport) void* __stdcall VirtualAlloc(void*,unsigned long long,unsigned long,unsigned long);
__declspec(dllimport) int   __stdcall VirtualFree(void*,unsigned long long,unsigned long);
__declspec(dllimport) int   __stdcall WriteFile(void*,const void*,unsigned long,unsigned long*,void*);
__declspec(dllimport) int   __stdcall ReadFile(void*,void*,unsigned long,unsigned long*,void*);
__declspec(dllimport) void* __stdcall GetStdHandle(unsigned long);
__declspec(dllimport) void  __stdcall ExitProcess(unsigned int);
__declspec(dllimport) int   __stdcall CreateFileA(const char*,unsigned long,unsigned long,void*,unsigned long,unsigned long,void*);
__declspec(dllimport) int   __stdcall CloseHandle(void*);
__declspec(dllimport) int   __stdcall GetFileSizeEx(void*,long long*);
__declspec(dllimport) int   __stdcall CreateDirectoryA(const char*,void*);

void *hy_alloc(hy_size bytes) {
    return VirtualAlloc(0,(unsigned long long)bytes,0x3000,0x04);
}
void hy_free(void *ptr,hy_size bytes) {
    (void)bytes; VirtualFree(ptr,0,0x8000);
}
hy_ssize hy_write(int fd,const void *buf,hy_size len) {
    void *h = GetStdHandle(fd==2 ? (unsigned long)-12 : (unsigned long)-11);
    unsigned long w=0; WriteFile(h,buf,(unsigned long)len,&w,0); return (hy_ssize)w;
}
hy_ssize hy_read(int fd,void *buf,hy_size len) {
    void *h = GetStdHandle((unsigned long)-10);
    unsigned long r=0; ReadFile(h,buf,(unsigned long)len,&r,0); return (hy_ssize)r;
}
void hy_exit(int code) { ExitProcess((unsigned int)code); }
int  hy_open(const char *path,int flags,int mode) {
    (void)mode;
    unsigned long access = (flags&1) ? 0x40000000 : 0x80000000;
    unsigned long create = (flags&1) ? 2 : 3;
    void *h = CreateFileA(path,access,0,0,create,0x80,0);
    return (h==(void*)-1) ? -1 : (int)(long long)h;
}
void hy_close(int fd) { CloseHandle((void*)(long long)fd); }
hy_ssize hy_pread(int fd,void *buf,hy_size len,long offset) {
    (void)fd;(void)buf;(void)len;(void)offset; return -1;
}
hy_ssize hy_pwrite(int fd,const void *buf,hy_size len,long offset) {
    (void)fd;(void)buf;(void)len;(void)offset; return -1;
}
long hy_fsize(int fd) {
    long long s=0; GetFileSizeEx((void*)(long long)fd,&s); return (long)s;
}
int hy_mkdir(const char *path,int mode) {
    (void)mode; return CreateDirectoryA(path,0) ? 0 : -1;
}
#endif

long hy_getcwd(char *buf, hy_size size) { (void)buf;(void)size; return -1; }
long hy_getenv(const char *name, char *buf, hy_size buf_len) { (void)name;(void)buf;(void)buf_len; return -1; }
