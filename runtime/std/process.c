#include "../platform/platform.h"

/* ── helpers ─────────────────────────────────────────────────────────────── */

static long hy_strlen(const char *s) { long n = 0; while (s[n]) n++; return n; }
static void *hy_memcpy(void *d, const void *s, long n) {
    char *dd = d; const char *ss = s; while (n--) *dd++ = *ss++; return d;
}
static void *hy_memset(void *d, int c, long n) {
    char *dd = d; while (n--) *dd++ = (char)c; return d;
}

/* ── argv builder ────────────────────────────────────────────────────────── */

/* Build a null-terminated argv array.
   argv_buf receives: [path_ptr, arg0_ptr, ..., argN_ptr, NULL]
   Returns the number of entries (path + args + 1 null). */
static long build_argv(const char *path, long path_len,
                       const char **args, long arg_count,
                       const char ***out_argv) {
    long n_entries = arg_count + 2; /* path + args + NULL */
    const char **argv = (const char **)hy_alloc((hy_size)(n_entries * sizeof(char *)));
    if (!argv) return -1;

    argv[0] = path;
    for (long i = 0; i < arg_count; i++) {
        argv[i + 1] = args[i];
    }
    argv[n_entries - 1] = (const char *)0;

    *out_argv = argv;
    return 0;
}

/* ── exit status ─────────────────────────────────────────────────────────── */

static int exit_status(int raw) {
    return (raw >> 8) & 0xFF;
}

/* ── spawn ───────────────────────────────────────────────────────────────── */

long hylian_spawn(char *path, long path_len,
                  char **args, long arg_count) {
    (void)path_len;
    const char **argv = (const char **)0;
    if (build_argv(path, path_len, (const char **)args, arg_count, &argv) < 0)
        return -1;

    long pid = hy_fork();
    if (pid < 0) return -1;

    if (pid == 0) {
        /* child */
        hy_execve(path, (char *const *)argv, (char *const *)0);
        hy_exit(127);
    }

    /* parent */
    int status = 0;
    long waited = hy_waitpid(pid, &status, 0);
    if (waited < 0) return -1;

    return exit_status(status);
}

/* ── output ──────────────────────────────────────────────────────────────── */

long hylian_output(char *path, long path_len,
                   char **args, long arg_count,
                   char *out_buf, long out_buf_len) {
    (void)path_len;
    if (!out_buf || out_buf_len <= 0) return -1;

    int pipe_fds[2];
    if (hy_pipe(pipe_fds) < 0) return -1;

    const char **argv = (const char **)0;
    if (build_argv(path, path_len, (const char **)args, arg_count, &argv) < 0) {
        hy_close(pipe_fds[0]);
        hy_close(pipe_fds[1]);
        return -1;
    }

    long pid = hy_fork();
    if (pid < 0) {
        hy_close(pipe_fds[0]);
        hy_close(pipe_fds[1]);
        return -1;
    }

    if (pid == 0) {
        /* child: redirect stdout to pipe */
        hy_close(pipe_fds[0]);
        hy_dup2(pipe_fds[1], 1);
        hy_close(pipe_fds[1]);
        hy_execve(path, (char *const *)argv, (char *const *)0);
        hy_exit(127);
    }

    /* parent: close write end, read from pipe */
    hy_close(pipe_fds[1]);

    long total = 0;
    long n;
    while (total < out_buf_len - 1) {
        n = hy_read(pipe_fds[0], out_buf + total,
                    (hy_size)(out_buf_len - 1 - total));
        if (n <= 0) break;
        total += n;
    }
    hy_close(pipe_fds[0]);
    out_buf[total] = '\0';

    /* wait for child */
    int status = 0;
    hy_waitpid(pid, &status, 0);

    return total;
}

/* ── status ──────────────────────────────────────────────────────────────── */

/* ProcessStatus layout (24 bytes):
   offset  0: long exit_code
   offset  8: char *stdout
   offset 16: char *stderr */

long hylian_status(char *path, long path_len,
                   char **args, long arg_count,
                   void **out_ps) {
    (void)path_len;
    if (!out_ps) return -1;

    int out_pipe[2], err_pipe[2];
    if (hy_pipe(out_pipe) < 0) return -1;
    if (hy_pipe(err_pipe) < 0) {
        hy_close(out_pipe[0]);
        hy_close(out_pipe[1]);
        return -1;
    }

    const char **argv = (const char **)0;
    if (build_argv(path, path_len, (const char **)args, arg_count, &argv) < 0) {
        hy_close(out_pipe[0]); hy_close(out_pipe[1]);
        hy_close(err_pipe[0]); hy_close(err_pipe[1]);
        return -1;
    }

    long pid = hy_fork();
    if (pid < 0) {
        hy_close(out_pipe[0]); hy_close(out_pipe[1]);
        hy_close(err_pipe[0]); hy_close(err_pipe[1]);
        return -1;
    }

    if (pid == 0) {
        /* child: redirect stdout and stderr */
        hy_close(out_pipe[0]);
        hy_close(err_pipe[0]);
        hy_dup2(out_pipe[1], 1);
        hy_dup2(err_pipe[1], 2);
        hy_close(out_pipe[1]);
        hy_close(err_pipe[1]);
        hy_execve(path, (char *const *)argv, (char *const *)0);
        hy_exit(127);
    }

    /* parent: close write ends */
    hy_close(out_pipe[1]);
    hy_close(err_pipe[1]);

    /* read stdout */
    long out_total = 0;
    char *out_buf = (char *)hy_alloc(4096);
    if (out_buf) {
        long n;
        while (1) {
            n = hy_read(out_pipe[0], out_buf + out_total, 4096 - 1 - out_total);
            if (n <= 0) break;
            out_total += n;
            if (out_total >= 4096 - 1) break;
        }
        out_buf[out_total] = '\0';
    }
    hy_close(out_pipe[0]);

    /* read stderr */
    long err_total = 0;
    char *err_buf = (char *)hy_alloc(4096);
    if (err_buf) {
        long n;
        while (1) {
            n = hy_read(err_pipe[0], err_buf + err_total, 4096 - 1 - err_total);
            if (n <= 0) break;
            err_total += n;
            if (err_total >= 4096 - 1) break;
        }
        err_buf[err_total] = '\0';
    }
    hy_close(err_pipe[0]);

    /* wait for child */
    int status = 0;
    hy_waitpid(pid, &status, 0);

    /* build ProcessStatus struct */
    char *ps = (char *)hy_alloc(24);
    if (!ps) return -1;
    hy_memset(ps, 0, 24);
    *(long *)(ps + 0)  = exit_status(status);
    *(char **)(ps + 8)  = out_buf;
    *(char **)(ps + 16) = err_buf;

    *out_ps = (void *)ps;
    return 0;
}

/* ── ProcessStatus accessors ─────────────────────────────────────────────── */

long ProcessStatus_exit_code(void *ps) {
    if (!ps) return -1;
    return *(long *)((char *)ps + 0);
}

char *ProcessStatus_stdout(void *ps) {
    if (!ps) return (char *)"";
    char *s = *(char **)((char *)ps + 8);
    return s ? s : (char *)"";
}

char *ProcessStatus_stderr(void *ps) {
    if (!ps) return (char *)"";
    char *s = *(char **)((char *)ps + 16);
    return s ? s : (char *)"";
}