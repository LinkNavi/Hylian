#include "../../platform/platform.h"

long hylian_getenv(char *name, long name_len, char *buf, long buf_len) {
    if (!name || name_len <= 0 || !buf || buf_len <= 0) return -1;
    /* ensure null-terminated name for hy_getenv */
    char tmp[256];
    if (name_len >= (long)sizeof(tmp)) return -1;
    long i = 0;
    while (i < name_len) { tmp[i] = name[i]; i++; }
    tmp[i] = '\0';
    return hy_getenv(tmp, buf, (hy_size)buf_len);
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
