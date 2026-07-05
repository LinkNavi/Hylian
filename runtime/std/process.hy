include {
    std.sys,
}

// ── helpers ───────────────────────────────────────────────────────────────────

int _str_len(str s) {
    int n = 0;
    while (true) {
        usize p = cast<usize>(s) + cast<usize>(n);
        uint8 c;
        unsafe { *uint8 bp = cast<*uint8>(p); c = *bp; }
        if (cast<int>(c) == 0) { return n; }
        n += 1;
    }
    return n;
}

void _str_copy(str dst, str src, int len) {
    int i = 0;
    while (i < len) {
        uint8 c;
        usize sp = cast<usize>(src) + cast<usize>(i);
        usize dp = cast<usize>(dst) + cast<usize>(i);
        unsafe { *uint8 sbp = cast<*uint8>(sp); c = *sbp; }
        unsafe { *uint8 dbp = cast<*uint8>(dp); *dbp = c; }
        i += 1;
    }
    usize dp2 = cast<usize>(dst) + cast<usize>(len);
    unsafe { *uint8 dbp2 = cast<*uint8>(dp2); *dbp2 = 0; }
}

str _str_alloc(int size) {
    return cast<str>(sys_mmap(size + 1));
}

// ── argv builder ──────────────────────────────────────────────────────────────

// Build a null-terminated argv array from a path and args list.
// Returns a pointer to the argv array (char **).
// Layout: [ptr0, ptr1, ..., ptrN, 0]
// Each ptr points to a null-terminated copy of the string.
str _build_argv(str path, str args, int arg_count) {
    // Allocate argv array: (arg_count + 2) pointers (path + args + null)
    int ptr_size = 8; // 64-bit pointers
    int argv_size = (arg_count + 2) * ptr_size;
    str argv_buf = _str_alloc(argv_size);

    // Write path as argv[0]
    usize argv_ptr = cast<usize>(argv_buf);
    unsafe { *cast<**uint8>(argv_ptr) = cast<*uint8>(path); }

    // Write each arg
    int i = 0;
    while (i < arg_count) {
        // args is str[] — an array of str pointers
        // Read the i-th pointer from the array
        usize arg_slot = cast<usize>(args) + cast<usize>(i * ptr_size);
        str arg;
        unsafe { arg = cast<str>(*cast<*usize>(arg_slot)); }
        usize slot = argv_ptr + cast<usize>((i + 1) * ptr_size);
        unsafe { *cast<**uint8>(slot) = cast<*uint8>(arg); }
        i += 1;
    }

    // Null terminator
    usize null_slot = argv_ptr + cast<usize>((arg_count + 1) * ptr_size);
    unsafe { *cast<*usize>(null_slot) = 0; }

    return argv_buf;
}

// ── wait helpers ──────────────────────────────────────────────────────────────

// Extract exit status from raw wait4 status.
// WEXITSTATUS(status) = (status >> 8) & 0xFF
int _exit_status(int raw_status) {
    return (raw_status >> 8) & 0xFF;
}

// ── public API ────────────────────────────────────────────────────────────────

int spawn(str path, str args, int arg_count) {
    str argv = _build_argv(path, args, arg_count);

    int pid = sys_fork();
    if (pid < 0) { return -1; }

    if (pid == 0) {
        // child
        sys_execve(path, argv, cast<str>(0));
        // execve failed
        sys_exit(127);
    }

    // parent
    str status_buf = _str_alloc(4);
    int waited = sys_wait4(pid, status_buf, 0, cast<str>(0));
    if (waited < 0) { return -1; }

    int raw;
    unsafe { raw = cast<int>(*cast<*int>(status_buf)); }
    return _exit_status(raw);
}

str output(str path, str args, int arg_count) {
    // Create pipe for stdout
    str pipe_fds = _str_alloc(8); // two ints = 8 bytes
    @target(linux)
    int pr = sys_pipe2(pipe_fds, 0);
    @target(macos)
    int pr = sys_pipe(pipe_fds);
    if (pr < 0) { return cast<str>(0); }

    int read_fd;
    int write_fd;
    unsafe {
        read_fd = cast<int>(*cast<*int>(pipe_fds));
        write_fd = cast<int>(*cast<*int>(cast<usize>(pipe_fds) + 4));
    }

    str argv = _build_argv(path, args, arg_count);

    int pid = sys_fork();
    if (pid < 0) {
        sys_close(read_fd);
        sys_close(write_fd);
        return cast<str>(0);
    }

    if (pid == 0) {
        // child: redirect stdout to pipe write end
        sys_close(read_fd);
        sys_dup2(write_fd, 1);
        sys_close(write_fd);
        sys_execve(path, argv, cast<str>(0));
        sys_exit(127);
    }

    // parent: close write end, read from pipe
    sys_close(write_fd);

    // Read output in chunks
    int chunk = 4096;
    str buf = _str_alloc(chunk);
    int total = 0;
    while (true) {
        str dst = cast<str>(cast<usize>(buf) + cast<usize>(total));
        int n = sys_read(read_fd, dst, chunk);
        if (n <= 0) { break; }
        total += n;
        // grow buffer if needed
        if (total + chunk > chunk * (1 + total / chunk)) {
            // realloc: mmap bigger, copy, munmap old
            int new_size = total + chunk;
            str new_buf = _str_alloc(new_size);
            _str_copy(new_buf, buf, total);
            sys_munmap(cast<int>(buf), chunk);
            buf = new_buf;
            chunk = new_size;
        }
    }
    sys_close(read_fd);

    // Null-terminate
    usize term = cast<usize>(buf) + cast<usize>(total);
    unsafe { *cast<*uint8>(term) = 0; }

    // Wait for child
    str status_buf = _str_alloc(4);
    sys_wait4(pid, status_buf, 0, cast<str>(0));

    return buf;
}

// ── ProcessStatus ─────────────────────────────────────────────────────────────

// Memory layout (24 bytes):
//   offset  0: int  exit_code
//   offset  8: str  stdout
//   offset 16: str  stderr

int ProcessStatus_exit_code(str ps) {
    if (cast<int>(ps) == 0) { return -1; }
    int code;
    unsafe { code = cast<int>(*cast<*int>(ps)); }
    return code;
}

str ProcessStatus_stdout(str ps) {
    if (cast<int>(ps) == 0) { return cast<str>(0); }
    str out;
    unsafe { out = cast<str>(*cast<*usize>(cast<usize>(ps) + 8)); }
    return out;
}

str ProcessStatus_stderr(str ps) {
    if (cast<int>(ps) == 0) { return cast<str>(0); }
    str err;
    unsafe { err = cast<str>(*cast<*usize>(cast<usize>(ps) + 16)); }
    return err;
}

str status(str path, str args, int arg_count) {
    // Create pipes for stdout and stderr
    str out_pipe = _str_alloc(8);
    str err_pipe = _str_alloc(8);

    @target(linux)
    int pr_out = sys_pipe2(out_pipe, 0);
    @target(macos)
    int pr_out = sys_pipe(out_pipe);
    if (pr_out < 0) { return cast<str>(0); }

    @target(linux)
    int pr_err = sys_pipe2(err_pipe, 0);
    @target(macos)
    int pr_err = sys_pipe(err_pipe);
    if (pr_err < 0) {
        // close out pipe fds
        int ofd0; int ofd1;
        unsafe {
            ofd0 = cast<int>(*cast<*int>(out_pipe));
            ofd1 = cast<int>(*cast<*int>(cast<usize>(out_pipe) + 4));
        }
        sys_close(ofd0);
        sys_close(ofd1);
        return cast<str>(0);
    }

    int out_read;  int out_write;
    int err_read;  int err_write;
    unsafe {
        out_read  = cast<int>(*cast<*int>(out_pipe));
        out_write = cast<int>(*cast<*int>(cast<usize>(out_pipe) + 4));
        err_read  = cast<int>(*cast<*int>(err_pipe));
        err_write = cast<int>(*cast<*int>(cast<usize>(err_pipe) + 4));
    }

    str argv = _build_argv(path, args, arg_count);

    int pid = sys_fork();
    if (pid < 0) {
        sys_close(out_read); sys_close(out_write);
        sys_close(err_read); sys_close(err_write);
        return cast<str>(0);
    }

    if (pid == 0) {
        // child: redirect stdout and stderr
        sys_close(out_read);
        sys_close(err_read);
        sys_dup2(out_write, 1);
        sys_dup2(err_write, 2);
        sys_close(out_write);
        sys_close(err_write);
        sys_execve(path, argv, cast<str>(0));
        sys_exit(127);
    }

    // parent: close write ends
    sys_close(out_write);
    sys_close(err_write);

    // Read stdout
    int chunk = 4096;
    str out_buf = _str_alloc(chunk);
    int out_total = 0;
    while (true) {
        str dst = cast<str>(cast<usize>(out_buf) + cast<usize>(out_total));
        int n = sys_read(out_read, dst, chunk);
        if (n <= 0) { break; }
        out_total += n;
    }
    sys_close(out_read);
    usize out_term = cast<usize>(out_buf) + cast<usize>(out_total);
    unsafe { *cast<*uint8>(out_term) = 0; }

    // Read stderr
    str err_buf = _str_alloc(chunk);
    int err_total = 0;
    while (true) {
        str dst = cast<str>(cast<usize>(err_buf) + cast<usize>(err_total));
        int n = sys_read(err_read, dst, chunk);
        if (n <= 0) { break; }
        err_total += n;
    }
    sys_close(err_read);
    usize err_term = cast<usize>(err_buf) + cast<usize>(err_total);
    unsafe { *cast<*uint8>(err_term) = 0; }

    // Wait for child
    str status_buf = _str_alloc(4);
    sys_wait4(pid, status_buf, 0, cast<str>(0));
    int raw;
    unsafe { raw = cast<int>(*cast<*int>(status_buf)); }
    int code = _exit_status(raw);

    // Build ProcessStatus struct (24 bytes)
    str ps = _str_alloc(24);
    unsafe {
        *cast<*int>(ps) = code;
        *cast<*usize>(cast<usize>(ps) + 8) = cast<usize>(out_buf);
        *cast<*usize>(cast<usize>(ps) + 16) = cast<usize>(err_buf);
    }
    return ps;
}

// ── exec ─────────────────────────────────────────────────────────────────────
//
// exec: run a program and return both its stdout and exit code.
// This is the convenience wrapper for init-system style usage where you want
// to capture the output of a command and check whether it succeeded.
//
// Returns a ProcessStatus (24-byte struct):
//   - exit code: ProcessStatus_exit_code(ps)
//   - stdout:    ProcessStatus_stdout(ps)
//   - stderr:    ProcessStatus_stderr(ps)
//
// Example:
//   str ps = exec("/bin/ls", args, 1);
//   int code = ProcessStatus_exit_code(ps);
//   str out  = ProcessStatus_stdout(ps);
//   if (code != 0) {
//       panic("ls failed");
//   }
//   print(out);
str exec(str path, str args, int arg_count) {
    return status(path, args, arg_count);
}
