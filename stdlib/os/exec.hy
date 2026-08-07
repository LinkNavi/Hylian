// os.exec — process creation and control.

include {
    platform.linux_x86_64,
    string,
}

// fork: create a child process. Returns 0 in the child, the child's PID in
// the parent, or -1 on error.
int fork() {
    return syscall(NR_FORK);
}

// pid: the calling process's own PID.
int pid() {
    return syscall(NR_GETPID);
}

// kill: send a signal to a process. Returns 0 on success, -1 on error.
int kill(int target_pid, int sig) {
    return syscall(NR_KILL, target_pid, sig);
}

// wait_raw: block until the given PID exits. `status_out` is the address of an
// 8-byte buffer that receives the kernel's raw wait status word (the kernel
// writes 32 bits there), or 0 to discard it. pid=-1 waits for any child.
// Returns the PID that was reaped, or -1 on error.
int wait_raw(int target_pid, usize status_out) {
    return syscall(NR_WAIT4, target_pid, cast<int>(status_out), 0, 0);
}

// exit_status_of: decode a raw wait status word into a shell-style exit code.
// Normal exit gives the process's own code; death by signal gives 128+signal,
// the same convention bash uses.
int exit_status_of(int status) {
    int low = status & 0x7f;
    if (low == 0) {
        // exited normally - code is in bits 8..15
        return (status >> 8) & 0xff;
    }
    // terminated by a signal
    return 128 + low;
}

// wait: block until the given PID exits and return its EXIT STATUS.
// pid=-1 waits for any child.
//
// This used to pass 0 for wait4's status pointer and return the syscall's own
// result, which is the reaped PID - so it returned a process ID while its
// documentation (and spawn(), built on top of it) claimed it was an exit
// status. Callers checking `spawn(...) == 0` for success saw a large nonzero
// number on every successful run.
int wait(int target_pid) {
    usize status_buf = raw_alloc(8);
    unsafe {
        *int64 zp = cast<*int64>(status_buf);
        *zp = cast<int64>(0);
    }

    int reaped = wait_raw(target_pid, status_buf);
    if (reaped < 0) {
        raw_free(status_buf, 8);
        return -1;
    }

    int32 raw;
    unsafe {
        *int32 sp = cast<*int32>(status_buf);
        raw = *sp;
    }
    raw_free(status_buf, 8);
    return exit_status_of(cast<int>(raw));
}

// _build_argv: pack path + a null-terminated array of arg strings into the
// char** layout execve expects: [ptr0, ptr1, ..., ptrN, nil].
// args_ptrs is the address of an existing array of `str` values in memory
// (arg_count of them, 8 bytes each) - build that array yourself with
// raw_alloc before calling this, same as building any other raw buffer.
static usize _build_argv(str path, usize args_ptrs, int arg_count) {
    int ptr_size = 8;
    usize argv = raw_alloc((arg_count + 2) * ptr_size);

    unsafe { *str p0 = cast<*str>(argv); *p0 = path; }

    int i = 0;
    while (i < arg_count) {
        usize src = args_ptrs + cast<usize>(i * ptr_size);
        usize dst = argv + cast<usize>((i + 1) * ptr_size);
        str s;
        unsafe {
            *str sp = cast<*str>(src); s = *sp;
            *str dp = cast<*str>(dst); *dp = s;
        }
        i += 1;
    }

    usize term = argv + cast<usize>((arg_count + 1) * ptr_size);
    unsafe { *str tp = cast<*str>(term); *tp = nil; }
    return argv;
}

// exec: replace the current process image. Does not return on success.
//   path      — executable path
//   args_ptrs — address of an array of `str` argv entries (not including
//               argv[0], which is `path` itself - see _build_argv)
//   arg_count — number of entries in args_ptrs
//   envp_ptrs — address of a null-terminated envp array, or 0 to pass an
//               empty environment
// Returns -1 on error (only returns at all if execve failed).
int exec(str path, usize args_ptrs, int arg_count, usize envp_ptrs) {
    usize argv = _build_argv(path, args_ptrs, arg_count);
    usize envp = envp_ptrs;
    if (envp == cast<usize>(0)) {
        envp = raw_alloc(8);
        unsafe { *str ep = cast<*str>(envp); *ep = nil; }
    }
    return syscall(NR_EXECVE, cast<int>(path), cast<int>(argv), cast<int>(envp));
}

// spawn: fork + exec + wait in one call - the common "run this program and
// block until it finishes" case. Returns the child's exit status, or -1 if
// fork/exec itself failed.
//
// `path` must be an actual path (containing a '/', or relative to the cwd).
// For shell-style lookup by bare command name, use spawn_path().
int spawn(str path, usize args_ptrs, int arg_count) {
    int child = fork();
    if (child < 0) { return -1; }

    if (child == 0) {
        exec(path, args_ptrs, arg_count, cast<usize>(0));
        // only reached if exec failed
        syscall(NR_EXIT, 127);
    }

    return wait(child);
}

// ── $PATH lookup ─────────────────────────────────────────────────────────────
//
// execve takes a path, not a command name - unlike execvp, it does no $PATH
// search of its own. So exec("echo", ...) fails with ENOENT even though `echo`
// is obviously on the system, which is not what anyone coming from a shell (or
// from Go's exec.Command, or Python's subprocess) expects. which() closes that
// gap, and exec_path()/spawn_path() are the execvp-shaped wrappers built on it.

// _read_path_env: the value of PATH from /proc/self/environ, or nil if it
// isn't set (or /proc isn't mounted).
//
// Reading /proc rather than a real getenv() is deliberate: Hylian has no
// access to the initial process stack and no environ binding, so this is the
// only way to see the environment using nothing but syscalls. It is Linux-only,
// which is fine - this module already includes platform.linux_x86_64.
static str _read_path_env() {
    str proc_path = "/proc/self/environ";
    int fd = syscall(NR_OPEN, cast<int>(proc_path), O_RDONLY, 0);
    if (fd < 0) { return nil; }

    // The environment block is small; one generous buffer avoids any resizing
    // logic. Anything beyond this is truncated rather than mis-parsed, because
    // the scan below always stops at a NUL.
    int cap = 65536;
    usize buf = raw_alloc(cap);
    int n = syscall(NR_READ, fd, cast<int>(buf), cap - 1);
    syscall(NR_CLOSE, fd, 0, 0);
    if (n <= 0) {
        raw_free(buf, cap);
        return nil;
    }
    unsafe {
        *uint8 endp = cast<*uint8>(buf + cast<usize>(n));
        *endp = cast<uint8>(0);
    }

    // Entries are NUL-separated "KEY=VALUE" strings. Walk entry by entry
    // looking for one that starts with "PATH=".
    int i = 0;
    while (i < n) {
        usize entry = buf + cast<usize>(i);

        // does this entry start with "PATH=" ?
        bool matched = true;
        int k = 0;
        while (k < 5) {
            uint8 want;
            if (k == 0) { want = cast<uint8>(80); }        // 'P'
            else if (k == 1) { want = cast<uint8>(65); }   // 'A'
            else if (k == 2) { want = cast<uint8>(84); }   // 'T'
            else if (k == 3) { want = cast<uint8>(72); }   // 'H'
            else { want = cast<uint8>(61); }               // '='
            uint8 got;
            unsafe {
                *uint8 gp = cast<*uint8>(entry + cast<usize>(k));
                got = *gp;
            }
            if (got != want) { matched = false; k = 5; }
            else { k += 1; }
        }
        if (matched) {
            // The value is already NUL-terminated in place, so it can be
            // handed back as a str directly - no copy needed. The buffer is
            // intentionally not freed: the returned str points into it.
            return cast<str>(entry + cast<usize>(5));
        }

        // skip to just past this entry's NUL
        while (i < n) {
            uint8 c;
            unsafe {
                *uint8 cp = cast<*uint8>(buf + cast<usize>(i));
                c = *cp;
            }
            i += 1;
            if (c == cast<uint8>(0)) { i = i; k = 0; break; }
        }
    }

    raw_free(buf, cap);
    return nil;
}

// DEFAULT_PATH: used when PATH is unset or unreadable. Same list the shell
// falls back to (see `getconf PATH`), so behaviour stays predictable in a
// stripped environment rather than silently failing every lookup.
static str DEFAULT_PATH = "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin";

// which: resolve a command name to a full executable path by searching $PATH,
// exactly as a shell would. Returns nil if nothing executable was found.
//
// A name that already contains '/' is returned unchanged if it is executable -
// this matches execvp, where "./build.sh" and "/bin/ls" bypass the search.
str which(str cmd) {
    if (cmd == nil) { return nil; }
    if (length(cmd) == 0) { return nil; }

    if (contains(cmd, "/")) {
        if (syscall(NR_ACCESS, cast<int>(cmd), X_OK, 0) == 0) { return cmd; }
        return nil;
    }

    str path_env = _read_path_env();
    if (path_env == nil) { path_env = DEFAULT_PATH; }

    int path_len = length(path_env);
    int start = 0;
    int i = 0;
    while (i <= path_len) {
        bool at_end = i == path_len;
        uint8 c = cast<uint8>(0);
        if (!at_end) {
            usize p = cast<usize>(path_env) + cast<usize>(i);
            unsafe {
                *uint8 cp = cast<*uint8>(p);
                c = *cp;
            }
        }

        // 58 = ':' - end of one PATH component
        if (at_end || c == cast<uint8>(58)) {
            if (i > start) {
                str dir = slice(path_env, start, i);
                str candidate = concat(concat(dir, "/"), cmd);
                if (syscall(NR_ACCESS, cast<int>(candidate), X_OK, 0) == 0) {
                    return candidate;
                }
            }
            start = i + 1;
        }
        i += 1;
    }

    return nil;
}

// exec_path: like exec(), but resolves `cmd` through $PATH first - the
// execvp() to exec()'s execve(). Returns -1 without execing if the command
// can't be found.
int exec_path(str cmd, usize args_ptrs, int arg_count, usize envp_ptrs) {
    str resolved = which(cmd);
    if (resolved == nil) { return -1; }
    return exec(resolved, args_ptrs, arg_count, envp_ptrs);
}

// spawn_path: like spawn(), but resolves `cmd` through $PATH first. This is
// the one that makes spawn_path("echo", ...) behave the way a shell would.
// Returns 127 (the shell's "command not found") if the lookup fails.
int spawn_path(str cmd, usize args_ptrs, int arg_count) {
    str resolved = which(cmd);
    if (resolved == nil) { return 127; }
    return spawn(resolved, args_ptrs, arg_count);
}

// ── stdio redirection ────────────────────────────────────────────────────────
//
// The dup2 equivalent of Go's cmd.Stdout/cmd.Stderr/cmd.Stdin. Without this,
// a spawned child always inherits the parent's descriptors verbatim and there
// is no way to capture or redirect its output from Hylian at all.

// dup2: make new_fd refer to the same open file as old_fd, closing whatever
// new_fd pointed at first. Returns new_fd, or -1 on error.
int dup2(int old_fd, int new_fd) {
    return syscall(NR_DUP2, old_fd, new_fd, 0);
}

// dup: lowest-numbered unused descriptor referring to the same open file.
int dup(int old_fd) {
    return syscall(NR_DUP, old_fd, 0, 0);
}

// pipe: create a pipe. `fds_out` is the address of a 2-int (8 bytes) buffer
// that receives [read_fd, write_fd]. Returns 0 on success, -1 on error.
//
// Note the kernel writes two 32-bit ints here, not two 64-bit ones, so the
// buffer is 8 bytes total and the two fds must be read back as int32.
int pipe(usize fds_out) {
    return syscall(NR_PIPE, cast<int>(fds_out), 0, 0);
}

// pipe_read_fd / pipe_write_fd: unpack the two 32-bit descriptors that pipe()
// wrote into `fds_out`, so callers don't have to hand-cast the buffer.
int pipe_read_fd(usize fds_out) {
    int32 r;
    unsafe {
        *int32 rp = cast<*int32>(fds_out);
        r = *rp;
    }
    return cast<int>(r);
}

int pipe_write_fd(usize fds_out) {
    int32 w;
    unsafe {
        *int32 wp = cast<*int32>(fds_out + cast<usize>(4));
        w = *wp;
    }
    return cast<int>(w);
}

// spawn_redirect: fork + exec + wait, with the child's stdin/stdout/stderr
// pointed at descriptors you supply. Pass -1 for any stream that should be
// inherited from the parent unchanged.
//
// The dup2 calls happen in the child, after fork and before exec, which is the
// only window where they affect the child alone.
//
//   usize fds = raw_alloc(8);
//   pipe(fds);
//   int out = pipe_write_fd(fds);
//   spawn_redirect("/bin/ls", args, n, -1, out, -1);
//
int spawn_redirect(str path, usize args_ptrs, int arg_count,
                   int fd_in, int fd_out, int fd_err) {
    int child = fork();
    if (child < 0) { return -1; }

    if (child == 0) {
        if (fd_in >= 0) { dup2(fd_in, STDIN_FD); }
        if (fd_out >= 0) { dup2(fd_out, STDOUT_FD); }
        if (fd_err >= 0) { dup2(fd_err, STDERR_FD); }
        exec(path, args_ptrs, arg_count, cast<usize>(0));
        syscall(NR_EXIT, 127);
    }

    return wait(child);
}

// spawn_path_redirect: spawn_redirect() with $PATH lookup on the command name.
int spawn_path_redirect(str cmd, usize args_ptrs, int arg_count,
                        int fd_in, int fd_out, int fd_err) {
    str resolved = which(cmd);
    if (resolved == nil) { return 127; }
    return spawn_redirect(resolved, args_ptrs, arg_count, fd_in, fd_out, fd_err);
}

// ── non-blocking variants ────────────────────────────────────────────────────
//
// spawn_redirect() waits for the child before returning, which is fine when the
// child's output goes to a terminal or a file, but DEADLOCKS if you point it at
// a pipe you intend to read afterwards: a pipe holds ~64KB, and once it fills
// the child blocks writing while the parent is still blocked in wait(). The
// parent has to be reading while the child runs.
//
// These return the child's PID immediately instead, leaving the caller to read
// and then wait() themselves. capture() below is the ready-made version.

// spawn_async_redirect: fork + exec with redirection, returning the child PID
// without waiting. Returns -1 if fork failed.
int spawn_async_redirect(str path, usize args_ptrs, int arg_count,
                         int fd_in, int fd_out, int fd_err) {
    int child = fork();
    if (child < 0) { return -1; }

    if (child == 0) {
        if (fd_in >= 0) { dup2(fd_in, STDIN_FD); }
        if (fd_out >= 0) { dup2(fd_out, STDOUT_FD); }
        if (fd_err >= 0) { dup2(fd_err, STDERR_FD); }
        exec(path, args_ptrs, arg_count, cast<usize>(0));
        syscall(NR_EXIT, 127);
    }

    return child;
}

// spawn_async: fork + exec, no redirection, no wait. Returns the child PID.
int spawn_async(str path, usize args_ptrs, int arg_count) {
    return spawn_async_redirect(path, args_ptrs, arg_count, -1, -1, -1);
}

// spawn_path_async: spawn_async() with $PATH lookup. Returns -1 if not found.
int spawn_path_async(str cmd, usize args_ptrs, int arg_count) {
    str resolved = which(cmd);
    if (resolved == nil) { return -1; }
    return spawn_async(resolved, args_ptrs, arg_count);
}

// close_fd: close a descriptor. Returns 0 on success, -1 on error.
int close_fd(int fd) {
    return syscall(NR_CLOSE, fd, 0, 0);
}

// read_fd: read up to `count` bytes into `buf`. Returns the byte count, 0 at
// end of file, or -1 on error.
int read_fd(int fd, usize buf, int count) {
    return syscall(NR_READ, fd, cast<int>(buf), count);
}

// write_fd: write `count` bytes from `buf`. Returns the byte count or -1.
int write_fd(int fd, usize buf, int count) {
    return syscall(NR_WRITE, fd, cast<int>(buf), count);
}

// capture: run a command and return everything it wrote to stdout as a str -
// the equivalent of Go's cmd.Output() / exec.Command(...).Output().
//
//   int status = 0;
//   str out = capture("ls", args, 1, &status);
//
// `status_out` receives the child's exit code (pass 0 to ignore it). Returns
// nil if the command couldn't be found or the pipe/fork failed.
//
// stderr is left pointing at the parent's, matching Go's Output() behaviour of
// not capturing stderr unless you ask.
str capture(str cmd, usize args_ptrs, int arg_count, usize status_out) {
    str resolved = which(cmd);
    if (resolved == nil) { return nil; }
    return _capture_resolved(resolved, args_ptrs, arg_count, status_out);
}

// _capture_resolved: capture() once the executable path is already known, so
// callers that resolved it themselves (Cmd.output()) don't pay for a second
// $PATH search.
str _capture_resolved(str resolved, usize args_ptrs, int arg_count, usize status_out) {
    usize fds = raw_alloc(8);
    if (pipe(fds) != 0) {
        raw_free(fds, 8);
        return nil;
    }
    int read_end = pipe_read_fd(fds);
    int write_end = pipe_write_fd(fds);
    raw_free(fds, 8);

    int child = spawn_async_redirect(resolved, args_ptrs, arg_count,
                                     -1, write_end, -1);
    if (child < 0) {
        close_fd(read_end);
        close_fd(write_end);
        return nil;
    }

    // The parent must close its own copy of the write end, or the pipe never
    // reaches EOF and the read loop below hangs forever after the child exits:
    // a pipe is only at EOF once EVERY write descriptor is closed, and the
    // parent is still holding one.
    close_fd(write_end);

    int cap = 65536;
    usize buf = raw_alloc(cap);
    int total = 0;
    bool reading = true;
    while (reading) {
        if (total >= cap - 1) {
            // Grow by doubling and copy across. Reading has to keep up with
            // the child, so this can't just stop at the first bufferful.
            int new_cap = cap * 2;
            usize bigger = raw_alloc(new_cap);
            int i = 0;
            while (i < total) {
                uint8 b;
                unsafe {
                    *uint8 sp = cast<*uint8>(buf + cast<usize>(i));
                    b = *sp;
                    *uint8 dp = cast<*uint8>(bigger + cast<usize>(i));
                    *dp = b;
                }
                i += 1;
            }
            raw_free(buf, cap);
            buf = bigger;
            cap = new_cap;
        }

        int n = read_fd(read_end, buf + cast<usize>(total), cap - 1 - total);
        if (n <= 0) { reading = false; }
        else { total += n; }
    }
    close_fd(read_end);

    unsafe {
        *uint8 endp = cast<*uint8>(buf + cast<usize>(total));
        *endp = cast<uint8>(0);
    }

    int status = wait(child);
    if (status_out != cast<usize>(0)) {
        unsafe {
            *int64 stp = cast<*int64>(status_out);
            *stp = cast<int64>(status);
        }
    }

    return cast<str>(buf);
}

// ── Cmd: the ergonomic front end ─────────────────────────────────────────────
//
// Everything above is the raw syscall layer: it works, but it makes you build
// an argv block out of raw_alloc and pointer stores by hand. This is the part
// you actually want to use, shaped after Go's os/exec:
//
//     Cmd c = new Cmd("ls");
//     c.arg("-l");
//     c.stdout_fd = STDOUT_FD;      // like cmd.Stdout = os.Stdout
//     c.stderr_fd = STDERR_FD;
//     int rc = c.run();
//
//     // or capture it, like cmd.Output()
//     Cmd c2 = new Cmd("ls");
//     c2.arg("-l");
//     str listing = c2.output();
//
// Arguments go in an array<str>, not a hand-packed pointer block. Descriptors
// are plain fields you assign. There is no separate function per combination of
// options — one class, a few fields, three verbs.

class Cmd {
    str        name;        // command name (looked up in $PATH) or a real path
    array<str> args;        // arguments AFTER argv[0]; argv[0] is `name` itself
    int        stdin_fd;    // -1 = inherit the parent's
    int        stdout_fd;   // -1 = inherit
    int        stderr_fd;   // -1 = inherit
    bool       search_path; // false = treat `name` as a literal path, no lookup

    Cmd(str name) {
        self.name = name;
        // An array field starts as whatever the allocator left behind, and
        // reading `.len` is a direct load with no null check, so an
        // uninitialised array field is a crash waiting to happen. Give it a
        // real empty array up front.
        self.args = [];
        self.stdin_fd = -1;
        self.stdout_fd = -1;
        self.stderr_fd = -1;
        self.search_path = true;
    }

    // arg: append one argument. Chainable in spirit, though Hylian has no
    // method chaining yet, so call it once per argument.
    void arg(str a) {
        self.args.push(a);
    }

    // resolved_path: the executable this Cmd will actually run, or nil if the
    // command name couldn't be found on $PATH.
    str resolved_path() {
        if (self.search_path) {
            return which(self.name);
        }
        return self.name;
    }

    // _argv_block: pack self.args into the flat array of `str` pointers that
    // exec() wants. This is the raw-pointer bookkeeping the old API made every
    // caller do; now it happens once, here.
    usize _argv_block() {
        int n = self.args.len;
        usize blk = raw_alloc((n + 1) * 8);
        int i = 0;
        while (i < n) {
            str a = self.args[i];
            unsafe {
                *str slot = cast<*str>(blk + cast<usize>(i * 8));
                *slot = a;
            }
            i += 1;
        }
        return blk;
    }

    // run: start the command and wait for it. Returns its exit status, or 127
    // if the command couldn't be found (the shell's convention).
    int run() {
        str path = self.resolved_path();
        if (path == nil) { return 127; }
        usize argv = self._argv_block();
        return spawn_redirect(path, argv, self.args.len,
                              self.stdin_fd, self.stdout_fd, self.stderr_fd);
    }

    // start: launch without waiting. Returns the child PID, or -1 on failure.
    // Pair it with wait_for().
    int start() {
        str path = self.resolved_path();
        if (path == nil) { return -1; }
        usize argv = self._argv_block();
        return spawn_async_redirect(path, argv, self.args.len,
                                    self.stdin_fd, self.stdout_fd, self.stderr_fd);
    }

    // wait_for: block until `pid` (from start()) exits; returns its status.
    int wait_for(int pid) {
        return wait(pid);
    }

    // output: run the command and return everything it wrote to stdout, like
    // Go's cmd.Output(). stdout_fd is ignored (the capture pipe takes it);
    // stderr still goes wherever stderr_fd says.
    str output() {
        str path = self.resolved_path();
        if (path == nil) { return nil; }
        usize argv = self._argv_block();
        return _capture_resolved(path, argv, self.args.len, cast<usize>(0));
    }

    // output_status: output() plus the exit code, for when you need both.
    str output_status(usize status_out) {
        str path = self.resolved_path();
        if (path == nil) { return nil; }
        usize argv = self._argv_block();
        return _capture_resolved(path, argv, self.args.len, status_out);
    }
}
