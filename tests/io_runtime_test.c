/* io_runtime_test.c — link test for Hylian runtime functions
 * Calls hylian_read_line, hylian_str_to_int, hylian_print, hylian_println,
 * hylian_int_to_str directly from C to verify they assemble and link correctly.
 *
 * Build (Linux):
 *   nasm -f elf64 runtime/std/io_linux.asm -o /tmp/io.o
 *   gcc tests/io_runtime_test.c /tmp/io.o -o /tmp/io_runtime_test -no-pie
 *   echo "42" | /tmp/io_runtime_test
 *
 * Expected output:
 *   print works
 *   println works
 *   int_to_str: 42 (len=2)
 *   read_line got: 42 (len=2)
 *   str_to_int: 42
 *   str_to_int negative: -99
 *   str_to_int whitespace: 7
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* Declarations matching the runtime's System V ABI interface */
extern void    hylian_print      (const char *str, int64_t len);
extern void    hylian_println    (const char *str, int64_t len);
extern int64_t hylian_int_to_str (int64_t n, char *buf, int64_t buflen);
extern int64_t hylian_read_line  (char *buf, int64_t buflen);
extern int64_t hylian_str_to_int (const char *str, int64_t len);

int main(void) {
    char buf[128];
    int64_t len;

    /* ── hylian_print ───────────────────────────────────────────────────── */
    {
        const char *msg = "print works";
        hylian_print(msg, (int64_t)strlen(msg));
        hylian_print("\n", 1);
    }

    /* ── hylian_println ─────────────────────────────────────────────────── */
    {
        const char *msg = "println works";
        hylian_println(msg, (int64_t)strlen(msg));
    }

    /* ── hylian_int_to_str ──────────────────────────────────────────────── */
    {
        len = hylian_int_to_str(42, buf, sizeof(buf));
        hylian_print("int_to_str: ", 12);
        hylian_print(buf, len);
        hylian_print(" (len=", 6);
        char lenbuf[8];
        int64_t llen = hylian_int_to_str(len, lenbuf, sizeof(lenbuf));
        hylian_print(lenbuf, llen);
        hylian_println(")", 1);

        /* Negative */
        len = hylian_int_to_str(-9223372036854775807LL, buf, sizeof(buf));
        hylian_print("int_to_str INT64_MIN+1: ", 24);
        hylian_println(buf, len);

        /* Zero */
        len = hylian_int_to_str(0, buf, sizeof(buf));
        hylian_print("int_to_str zero: ", 17);
        hylian_println(buf, len);
    }

    /* ── hylian_read_line ───────────────────────────────────────────────── */
    {
        len = hylian_read_line(buf, sizeof(buf));
        hylian_print("read_line got: ", 15);
        hylian_print(buf, len);
        hylian_print(" (len=", 6);
        char lenbuf[8];
        int64_t llen = hylian_int_to_str(len, lenbuf, sizeof(lenbuf));
        hylian_print(lenbuf, llen);
        hylian_println(")", 1);
    }

    /* ── hylian_str_to_int ──────────────────────────────────────────────── */
    {
        /* The line we just read */
        int64_t n = hylian_str_to_int(buf, len);
        char outbuf[32];
        int64_t olen = hylian_int_to_str(n, outbuf, sizeof(outbuf));
        hylian_print("str_to_int: ", 12);
        hylian_println(outbuf, olen);

        /* Negative string */
        const char *neg = "-99";
        n = hylian_str_to_int(neg, 3);
        olen = hylian_int_to_str(n, outbuf, sizeof(outbuf));
        hylian_print("str_to_int negative: ", 21);
        hylian_println(outbuf, olen);

        /* Leading whitespace + plus sign */
        const char *ws = "   +7";
        n = hylian_str_to_int(ws, 5);
        olen = hylian_int_to_str(n, outbuf, sizeof(outbuf));
        hylian_print("str_to_int whitespace: ", 23);
        hylian_println(outbuf, olen);

        /* Non-digit stop */
        const char *mixed = "123abc";
        n = hylian_str_to_int(mixed, 6);
        olen = hylian_int_to_str(n, outbuf, sizeof(outbuf));
        hylian_print("str_to_int stops at non-digit: ", 31);
        hylian_println(outbuf, olen);
    }

    return 0;
}