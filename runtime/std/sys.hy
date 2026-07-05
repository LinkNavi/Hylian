// ── syscall numbers ───────────────────────────────────────────────────────────

@target(linux)
static int _SYS_READ   = 0;
@target(linux)
static int _SYS_WRITE  = 1;
@target(linux)
static int _SYS_OPEN   = 2;
@target(linux)
static int _SYS_CLOSE  = 3;
@target(linux)
static int _SYS_FSTAT  = 5;
@target(linux)
static int _SYS_MMAP   = 9;
@target(linux)
static int _SYS_MUNMAP = 11;
@target(linux)
static int _SYS_GETCWD = 79;
@target(linux)
static int _SYS_MKDIR  = 83;
@target(linux)
static int _SYS_PREAD  = 17;
@target(linux)
static int _SYS_PWRITE = 18;
@target(linux)
static int _SYS_EXIT   = 60;
@target(linux)
static int _SYS_FORK   = 57;
@target(linux)
static int _SYS_EXECVE = 59;
@target(linux)
static int _SYS_WAIT4  = 61;
@target(linux)
static int _SYS_PIPE2  = 293;
@target(linux)
static int _SYS_DUP2   = 33;

@target(linux)
static int _SYS_MOUNT  = 165;
@target(linux)
static int _SYS_UMOUNT2 = 166;
@target(linux)
static int _SYS_REBOOT = 169;

// reboot magic constants (Linux)
@target(linux)
static int _LINUX_REBOOT_MAGIC1     = 0xfee1dead;
@target(linux)
static int _LINUX_REBOOT_MAGIC2     = 672274793;
@target(linux)
static int _LINUX_REBOOT_MAGIC2A    = 85072278;
@target(linux)
static int _LINUX_REBOOT_CMD_RESTART  = 0x01234567;
@target(linux)
static int _LINUX_REBOOT_CMD_HALT     = 0xCDEF0123;
@target(linux)
static int _LINUX_REBOOT_CMD_CAD_ON   = 0x89ABCDEF;
@target(linux)
static int _LINUX_REBOOT_CMD_CAD_OFF  = 0x00000000;
@target(linux)
static int _LINUX_REBOOT_CMD_POWER_OFF = 0x4321FEDC;

@target(macos)
static int _SYS_READ   = 0x2000003;
@target(macos)
static int _SYS_WRITE  = 0x2000004;
@target(macos)
static int _SYS_OPEN   = 0x2000005;
@target(macos)
static int _SYS_CLOSE  = 0x2000006;
@target(macos)
static int _SYS_FSTAT  = 0x20000BD;
@target(macos)
static int _SYS_MMAP   = 0x20000C5;
@target(macos)
static int _SYS_MUNMAP = 0x2000049;
@target(macos)
static int _SYS_GETCWD = 0x2000150;
@target(macos)
static int _SYS_MKDIR  = 0x2000088;
@target(macos)
static int _SYS_PREAD  = 0x2000099;
@target(macos)
static int _SYS_PWRITE = 0x200009A;
@target(macos)
static int _SYS_EXIT   = 0x2000001;
@target(macos)
static int _SYS_FORK   = 0x2000002;
@target(macos)
static int _SYS_EXECVE = 0x200003B;
@target(macos)
static int _SYS_WAIT4  = 0x2000007;
@target(macos)
static int _SYS_PIPE   = 0x200002A;
@target(macos)
static int _SYS_DUP2   = 0x200005A;

// MAP flags
@target(linux)
static int _MAP_FLAGS = 0x22;
@target(macos)
static int _MAP_FLAGS = 0x1002;

// ── 3-arg syscall (covers most cases) ────────────────────────────────────────

naked int _sc3(int n, int a, int b, int c) {
    asm {
        mov rax, rdi
        mov rdi, rsi
        mov rsi, rdx
        mov rdx, rcx
        syscall
        ret
    }
}

// ── 6-arg syscalls (each has its own naked wrapper — avoids stack arg issues) -

naked int _sc_pread(int n, int fd, int buf, int len, int off) {
    asm {
        mov rax, rdi
        mov rdi, rsi
        mov rsi, rdx
        mov rdx, rcx
        mov r10, r8
        xor r8,  r8
        xor r9,  r9
        syscall
        ret
    }
}

naked int _sc_pwrite(int n, int fd, int buf, int len, int off) {
    asm {
        mov rax, rdi
        mov rdi, rsi
        mov rsi, rdx
        mov rdx, rcx
        mov r10, r8
        xor r8,  r8
        xor r9,  r9
        syscall
        ret
    }
}

// 5-arg syscall (used by mount: source, target, fstype, flags, data)
naked int _sc5(int n, int a, int b, int c, int d) {
    asm {
        mov rax, rdi
        mov rdi, rsi
        mov rsi, rdx
        mov rdx, rcx
        mov r10, r8
        xor r8,  r8
        xor r9,  r9
        syscall
        ret
    }
}

naked usize _sc_mmap(int n, int len, int prot, int flags) {
    asm {
        ; rdi=n, rsi=len, rdx=prot, rcx=flags
        mov rax, rdi
        xor rdi, rdi    ; addr = 0
        mov rsi, rsi    ; size = len (already in rsi)
        mov rdx, rdx    ; prot (already in rdx)
        mov r10, rcx    ; flags
        mov r8,  -1     ; fd = -1
        xor r9,  r9     ; offset = 0
        syscall
        ret
    }
}

// ── public wrappers ───────────────────────────────────────────────────────────

int sys_write(int fd, str buf, int len) {
    return _sc3(_SYS_WRITE, fd, cast<int>(buf), len);
}

int sys_read(int fd, str buf, int len) {
    return _sc3(_SYS_READ, fd, cast<int>(buf), len);
}

int sys_open(str path, int flags, int mode) {
    return _sc3(_SYS_OPEN, cast<int>(path), flags, mode);
}

void sys_close(int fd) {
    _sc3(_SYS_CLOSE, fd, 0, 0);
}

void sys_exit(int code) {
    _sc3(_SYS_EXIT, code, 0, 0);
}

usize sys_mmap(int len) {
    return _sc_mmap(_SYS_MMAP, len, 3, _MAP_FLAGS);
}

void sys_munmap(int ptr, int len) {
    _sc3(_SYS_MUNMAP, ptr, len, 0);
}

int sys_getcwd(str buf, int size) {
    return _sc3(_SYS_GETCWD, cast<int>(buf), size, 0);
}

int sys_mkdir(str path, int mode) {
    return _sc3(_SYS_MKDIR, cast<int>(path), mode, 0);
}

int sys_pread(int fd, str buf, int len, int offset) {
    return _sc_pread(_SYS_PREAD, fd, cast<int>(buf), len, offset);
}

int sys_pwrite(int fd, str buf, int len, int offset) {
    return _sc_pwrite(_SYS_PWRITE, fd, cast<int>(buf), len, offset);
}

// ── process syscalls ───────────────────────────────────────────────────────

int sys_fork() {
    return _sc3(_SYS_FORK, 0, 0, 0);
}

int sys_execve(str path, str argv, str envp) {
    return _sc3(_SYS_EXECVE, cast<int>(path), cast<int>(argv), cast<int>(envp));
}

int sys_wait4(int pid, str status, int options, str rusage) {
    return _sc3(_SYS_WAIT4, pid, cast<int>(status), cast<int>(rusage));
}

@target(linux)
int sys_pipe2(str fds, int flags) {
    return _sc3(_SYS_PIPE2, cast<int>(fds), flags, 0);
}

@target(macos)
int sys_pipe(str fds) {
    return _sc3(_SYS_PIPE, cast<int>(fds), 0, 0);
}

int sys_dup2(int oldfd, int newfd) {
    return _sc3(_SYS_DUP2, oldfd, newfd, 0);
}

// ── mount / power syscalls (Linux only) ──────────────────────────────────────

@target(linux)
int sys_mount(str source, str target, str fstype, int flags, str data) {
    return _sc5(_SYS_MOUNT, cast<int>(source), cast<int>(target),
                cast<int>(fstype), flags, cast<int>(data));
}

@target(linux)
int sys_umount2(str target, int flags) {
    return _sc3(_SYS_UMOUNT2, cast<int>(target), flags, 0);
}

@target(linux)
int sys_reboot(int magic1, int magic2, int cmd, int arg) {
    return _sc3(_SYS_REBOOT, magic1, magic2, cmd);
}
