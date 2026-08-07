// platform.linux_x86_64 — Linux/x86-64 syscall numbers and flag constants.
//
// This is the ONLY platform file for v0.1.0 (Hylian currently targets
// Linux/x86-64 exclusively). Every module above this one (time, os.*, io,
// net, string) calls the `syscall(nr, ...)` compiler builtin directly using
// the constant names defined here — none of them contain a raw number or
// know they're "on Linux". A future port (another OS, another arch) means
// adding a sibling file with the same constant names but different values;
// nothing above this file should need to change.

// ── syscall numbers ──────────────────────────────────────────────────────────

static int NR_READ          = 0;
static int NR_WRITE         = 1;
static int NR_OPEN          = 2;
static int NR_CLOSE         = 3;
static int NR_STAT          = 4;
static int NR_FSTAT         = 5;
static int NR_LSTAT         = 6;
static int NR_MMAP          = 9;
static int NR_MPROTECT      = 10;
static int NR_MUNMAP        = 11;
static int NR_BRK           = 12;
static int NR_IOCTL         = 16;
static int NR_PREAD64       = 17;
static int NR_PWRITE64      = 18;
static int NR_ACCESS        = 21;
static int NR_PIPE          = 22;
static int NR_DUP           = 32;
static int NR_DUP2          = 33;
static int NR_NANOSLEEP     = 35;
static int NR_GETPID        = 39;
static int NR_SOCKET        = 41;
static int NR_CONNECT       = 42;
static int NR_ACCEPT        = 43;
static int NR_SENDTO        = 44;
static int NR_RECVFROM      = 45;
static int NR_SHUTDOWN      = 48;
static int NR_BIND          = 49;
static int NR_LISTEN        = 50;
static int NR_SOCKETPAIR    = 53;
static int NR_SETSOCKOPT    = 54;
static int NR_GETSOCKOPT    = 55;
static int NR_FORK          = 57;
static int NR_EXECVE        = 59;
static int NR_EXIT          = 60;
static int NR_WAIT4         = 61;
static int NR_KILL          = 62;
static int NR_UNAME         = 63;
static int NR_FCNTL         = 72;
static int NR_GETCWD        = 79;
static int NR_CHDIR         = 80;
static int NR_MKDIR         = 83;
static int NR_RMDIR         = 84;
static int NR_UNLINK        = 87;
static int NR_READLINK      = 89;
static int NR_CHMOD         = 90;
static int NR_CHOWN         = 92;
static int NR_SYSINFO       = 99;
static int NR_GETTIMEOFDAY  = 96;
static int NR_GETUID        = 102;
static int NR_GETGID        = 104;
static int NR_SETUID        = 105;
static int NR_SETGID        = 106;
static int NR_GETEUID       = 107;
static int NR_GETEGID       = 108;
static int NR_CLOCK_GETTIME = 228;
static int NR_EXIT_GROUP    = 231;
static int NR_PIPE2         = 293;
static int NR_MOUNT         = 165;
static int NR_UMOUNT2       = 166;
static int NR_REBOOT        = 169;

// ── mmap prot / flags ────────────────────────────────────────────────────────

static int PROT_READ     = 1;
static int PROT_WRITE    = 2;
static int PROT_EXEC     = 4;
static int MAP_PRIVATE   = 2;
static int MAP_ANONYMOUS = 0x20;

// ── open() flags ─────────────────────────────────────────────────────────────

static int O_RDONLY = 0;
static int O_WRONLY = 1;
static int O_RDWR   = 2;
static int O_CREAT  = 0x40;
static int O_TRUNC  = 0x200;
static int O_APPEND = 0x400;

// ── access() mode bits ───────────────────────────────────────────────────────

static int F_OK = 0;
static int X_OK = 1;
static int W_OK = 2;
static int R_OK = 4;

// ── standard file descriptors ────────────────────────────────────────────────

static int STDIN_FD  = 0;
static int STDOUT_FD = 1;
static int STDERR_FD = 2;

// ── mount() flags ────────────────────────────────────────────────────────────

static int MS_RDONLY = 1;
static int MS_NOSUID = 2;
static int MS_NODEV  = 4;
static int MS_NOEXEC = 8;

// ── reboot() magic numbers / commands ────────────────────────────────────────

static int REBOOT_MAGIC1        = 0xfee1dead;
static int REBOOT_MAGIC2        = 672274793;
static int REBOOT_MAGIC2A       = 85072278;
static int REBOOT_CMD_RESTART   = 0x01234567;
static int REBOOT_CMD_HALT      = 0xCDEF0123;
static int REBOOT_CMD_POWER_OFF = 0x4321FEDC;
static int REBOOT_CMD_CAD_ON    = 0x89ABCDEF;
static int REBOOT_CMD_CAD_OFF   = 0x00000000;

// ── socket() family / type ───────────────────────────────────────────────────

static int AF_INET     = 2;
static int SOCK_STREAM = 1;
static int SOCK_DGRAM  = 2;

// ── shared low-level allocator ───────────────────────────────────────────────
// Every module that needs scratch memory (argv arrays, path buffers, socket
// address structs, timespec buffers...) goes through this instead of each
// calling mmap directly - keeps the actual syscall(NR_MMAP, ...) argument
// order in exactly one place.

usize raw_alloc(int size) {
    return cast<usize>(syscall(NR_MMAP, 0, size, PROT_READ + PROT_WRITE,
                                MAP_PRIVATE + MAP_ANONYMOUS, -1, 0));
}

void raw_free(usize ptr, int size) {
    syscall(NR_MUNMAP, cast<int>(ptr), size, 0);
}
