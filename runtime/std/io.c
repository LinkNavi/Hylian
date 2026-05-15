#include "../platform/platform.h"

void hylian_print(char *str, long len) {
    if (!str || len <= 0) return;
    hy_write(1, str, len);
}

void hylian_println(char *str, long len) {
    if (str && len > 0) hy_write(1, str, len);
    hy_write(1, "\n", 1);
}

long hylian_int_to_str(long n, char *buf, long buflen) {
    if (!buf || buflen <= 0) return 0;
    char tmp[24]; int tlen=0, neg=0;
    unsigned long u;
    if (n < 0) { neg=1; u=(unsigned long)(-(n+1))+1u; } else u=(unsigned long)n;
    if (u==0) tmp[tlen++]='0';
    else while(u>0){ tmp[tlen++]=(char)('0'+(int)(u%10)); u/=10; }
    if (neg) tmp[tlen++]='-';
    if ((long)tlen > buflen) return 0;
    for (int i=0;i<tlen;i++) buf[i]=tmp[tlen-1-i];
    return (long)tlen;
}

/* Convert a double to a null-terminated string in buf (up to buflen bytes).
   Returns the number of characters written (not including the null terminator).
   Uses a simple snprintf-style approach via the platform write helpers.
   The caller is responsible for buf being at least buflen bytes. */
long hylian_float_to_str(char *buf, long buflen, double value) {
    if (!buf || buflen <= 0) return 0;
    /* We produce the representation manually to avoid depending on printf.
       Format: up to 6 significant decimal digits, stripping trailing zeros. */
    char tmp[64];
    int neg = 0;
    if (value < 0.0) { neg = 1; value = -value; }
    /* Separate integer and fractional parts */
    long long ipart = (long long)value;
    double fpart = value - (double)ipart;
    /* Write integer part */
    int tlen = 0;
    if (ipart == 0) {
        tmp[tlen++] = '0';
    } else {
        char ibuf[24]; int ilen = 0;
        long long u = ipart;
        while (u > 0) { ibuf[ilen++] = (char)('0' + (int)(u % 10)); u /= 10; }
        for (int i = ilen - 1; i >= 0; i--) tmp[tlen++] = ibuf[i];
    }
    tmp[tlen++] = '.';
    /* Write 6 fractional digits */
    for (int d = 0; d < 6; d++) {
        fpart *= 10.0;
        int digit = (int)fpart;
        tmp[tlen++] = (char)('0' + digit);
        fpart -= (double)digit;
    }
    /* Strip trailing zeros after the dot (keep at least one) */
    while (tlen > 2 && tmp[tlen - 1] == '0') tlen--;
    tmp[tlen] = '\0';
    /* Prepend minus sign if needed */
    int out = 0;
    if (neg && out < buflen - 1) buf[out++] = '-';
    for (int i = 0; i < tlen && out < buflen - 1; i++) buf[out++] = tmp[i];
    buf[out] = '\0';
    return (long)out;
}

long hylian_read_line(char *buf, long buflen) {
    if (!buf || buflen <= 0) return 0;
    long out = 0;
    char c;
    while (out < buflen - 1) {
        long n = hy_read(0, &c, 1);
        if (n <= 0) break;
        if (c == '\n') break;
        if (c == '\r') continue;
        buf[out++] = c;
    }
    return out;
}

long hylian_str_to_int(char *str, long len) {
    if (!str || len <= 0) return 0;
    long i=0;
    while(i<len && (str[i]==' '||str[i]=='\t'||str[i]=='\n'||str[i]=='\r')) i++;
    if (i>=len) return 0;
    long sign=1;
    if (str[i]=='-'){sign=-1;i++;} else if(str[i]=='+'){i++;}
    long r=0;
    while(i<len && str[i]>='0' && str[i]<='9'){ r=r*10+(long)(str[i]-'0'); i++; }
    return sign*r;
}
