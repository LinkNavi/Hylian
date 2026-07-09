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

// wait: block until the given PID exits, returning its exit status.
// pid=-1 waits for any child.
int wait(int target_pid) {
    return syscall(NR_WAIT4, target_pid, 0, 0, 0);
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
