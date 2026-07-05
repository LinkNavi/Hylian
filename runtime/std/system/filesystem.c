#include "../../platform/platform.h"

// O_RDONLY=0 O_WRONLY=1 O_CREAT=64 O_TRUNC=512 O_APPEND=1024
#ifdef _WIN32
#  define HY_O_RDONLY 0
#  define HY_O_WRONLY 1
#  define HY_O_CREAT  0x100
#  define HY_O_TRUNC  0x200
#  define HY_O_APPEND 0x400
#else
#  define HY_O_RDONLY 0
#  define HY_O_WRONLY 1
#  define HY_O_CREAT  64
#  define HY_O_TRUNC  512
#  define HY_O_APPEND 1024
#endif

static long hy_strlen(const char *s){ long n=0; while(s[n])n++; return n; }
static void hy_memcpy(void *d,const void *s,long n){ char*dd=d;const char*ss=s; while(n--)*dd++=*ss++; }

long hylian_file_read(char *path,long path_len,char *buf,long buf_len) {
    if(!path||path_len<=0||!buf||buf_len<=0)return -1;
    char tmp[4097]; if(path_len>4096)return -1;
    hy_memcpy(tmp,path,path_len); tmp[path_len]='\0';
    int fd=hy_open(tmp,HY_O_RDONLY,0); if(fd<0)return -1;
    long total=0,n;
    while(total<buf_len){
        n=hy_pread(fd,buf+total,(hy_size)(buf_len-total),total);
        if(n<=0)break; total+=n;
    }
    hy_close(fd); return total;
}

long hylian_file_write(char *path,long path_len,char *buf,long buf_len) {
    if(!path||path_len<=0||!buf||buf_len<=0)return -1;
    char tmp[4097]; if(path_len>4096)return -1;
    hy_memcpy(tmp,path,path_len); tmp[path_len]='\0';
    int fd=hy_open(tmp,HY_O_WRONLY|HY_O_CREAT|HY_O_TRUNC,0644); if(fd<0)return -1;
    long n=hy_pwrite(fd,buf,(hy_size)buf_len,0);
    hy_close(fd); return n;
}

long hylian_file_append(char *path,long path_len,char *buf,long buf_len) {
    if(!path||path_len<=0||!buf||buf_len<=0)return -1;
    char tmp[4097]; if(path_len>4096)return -1;
    hy_memcpy(tmp,path,path_len); tmp[path_len]='\0';
    int fd=hy_open(tmp,HY_O_WRONLY|HY_O_CREAT|HY_O_APPEND,0644); if(fd<0)return -1;
    long size=hy_fsize(fd);
    long n=hy_pwrite(fd,buf,(hy_size)buf_len,size);
    hy_close(fd); return n;
}

long hylian_file_exists(char *path,long path_len) {
    if(!path||path_len<=0)return 0;
    char tmp[4097]; if(path_len>4096)return 0;
    hy_memcpy(tmp,path,path_len); tmp[path_len]='\0';
    int fd=hy_open(tmp,HY_O_RDONLY,0); if(fd<0)return 0;
    hy_close(fd); return 1;
}

long hylian_file_size(char *path,long path_len) {
    if(!path||path_len<=0)return -1;
    char tmp[4097]; if(path_len>4096)return -1;
    hy_memcpy(tmp,path,path_len); tmp[path_len]='\0';
    int fd=hy_open(tmp,HY_O_RDONLY,0); if(fd<0)return -1;
    long s=hy_fsize(fd); hy_close(fd); return s;
}

long hylian_getcwd(char *buf, long buf_len) {
    if (!buf || buf_len <= 0) return -1;
    return hy_getcwd(buf, (hy_size)buf_len);
}
long hylian_parent_dir(char *path,long path_len,char *buf,long buf_len) {
    if(!path||path_len<=0||!buf||buf_len<=0)return -1;
    char *last=0; for(long i=0;i<path_len;i++) if(path[i]=='/'||path[i]=='\\')last=path+i;
    if(!last){if(buf_len<2)return -1;buf[0]='.';buf[1]='\0';return 1;}
    long l=last-path; if(l>=buf_len)return -1;
    hy_memcpy(buf,path,l); buf[l]='\0'; return l;
}
long hylian_mkdir(char *path,long path_len) {
    if(!path||path_len<=0)return -1;
    char tmp[4097]; if(path_len>4096)return -1;
    hy_memcpy(tmp,path,path_len); tmp[path_len]='\0';
    return hy_mkdir(tmp,0755);
}

long hylian_mount(char *source,long source_len,char *target,long target_len,char *fstype,long fstype_len,long flags) {
    char src[256],dst[256],fst[64];
    if(source_len>0){ if(source_len>255)return -1; hy_memcpy(src,source,source_len); src[source_len]='\0'; }
    else { src[0]='\0'; }
    if(!target||target_len<=0||target_len>255)return -1;
    hy_memcpy(dst,target,target_len); dst[target_len]='\0';
    if(!fstype||fstype_len<=0||fstype_len>63)return -1;
    hy_memcpy(fst,fstype,fstype_len); fst[fstype_len]='\0';
    return hy_mount(src[0]?src:0, dst, fst, (unsigned long)flags, 0);
}

long hylian_umount(char *target,long target_len) {
    if(!target||target_len<=0)return -1;
    char tmp[257]; if(target_len>256)return -1;
    hy_memcpy(tmp,target,target_len); tmp[target_len]='\0';
    return hy_umount(tmp, 0);
}

long hylian_reboot(long cmd) {
    return hy_reboot((int)cmd);
}
