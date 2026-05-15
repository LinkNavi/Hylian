#ifndef HYLIAN_PLATFORM_H
#define HYLIAN_PLATFORM_H

typedef long          hy_ssize;
typedef unsigned long hy_size;

void    *hy_alloc(hy_size bytes);
void     hy_free(void *ptr, hy_size bytes);
hy_ssize hy_write(int fd, const void *buf, hy_size len);
hy_ssize hy_read(int fd, void *buf, hy_size len);
void     hy_exit(int code);
int      hy_open(const char *path, int flags, int mode);
void     hy_close(int fd);
hy_ssize hy_pread(int fd, void *buf, hy_size len, long offset);
hy_ssize hy_pwrite(int fd, const void *buf, hy_size len, long offset);
long     hy_fsize(int fd);
int      hy_mkdir(const char *path, int mode);

#endif
