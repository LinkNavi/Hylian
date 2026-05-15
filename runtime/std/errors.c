#include "../platform/platform.h"

void *hylian_make_error(char *msg, long len) {
    if (!msg || len < 0) return (void*)0;
    char *message = (char*)hy_alloc((hy_size)(len + 1));
    if (!message) return (void*)0;
    for (long i = 0; i < len; i++) message[i] = msg[i];
    message[len] = '\0';
    char **err = (char**)hy_alloc(16);
    if (!err) return (void*)0;
    err[0] = message;
    *((long*)(err + 1)) = 0;
    return (void*)err;
}

char *Error_message(void *err) {
    if (!err) return (char*)"";
    return ((char**)err)[0];
}

long Error_code(void *err) {
    if (!err) return 0;
    return *((long*)(((char**)err) + 1));
}

void hylian_panic(char *msg, long len) {
    const char pre[] = "panic: ";
    hy_write(2, pre, 7);
    if (msg && len > 0) hy_write(2, msg, len);
    hy_write(2, "\n", 1);
    hy_exit(1);
}
