#include "../../platform/platform.h"

long hylian_getenv(char *name, long name_len, char *buf, long buf_len) {
    (void)name;(void)name_len;(void)buf;(void)buf_len;
    return -1; // needs libc on all platforms, stub for now
}
void hylian_exit(long code) { hy_exit((int)code); }
long hylian_exec(char *cmd, long cmd_len) { (void)cmd;(void)cmd_len; return -1; }
long hylian_os(char *buf, long buf_len) {
    if (!buf || buf_len <= 0) return -1;
#if defined(__linux__)
    const char *n = "linux";
#elif defined(__APPLE__)
    const char *n = "macos";
#elif defined(_WIN32)
    const char *n = "windows";
#else
    const char *n = "unknown";
#endif
    long i=0; while(n[i]&&i<buf_len-1){buf[i]=n[i];i++;} buf[i]='\0'; return i;
}
