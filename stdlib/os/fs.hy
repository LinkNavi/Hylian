// os.fs — filesystem operations over raw syscalls.

include {
    platform.linux_x86_64,
    string,
}

// open: open a file, returns a file descriptor or a negative errno on error.
int open(str path, int flags, int mode) {
    return syscall(NR_OPEN, cast<int>(path), flags, mode);
}

// create: convenience wrapper - open for writing, creating/truncating.
int create(str path, int mode) {
    return open(path, O_WRONLY + O_CREAT + O_TRUNC, mode);
}

// close: close a file descriptor.
void close(int fd) {
    syscall(NR_CLOSE, fd);
}

// read: read up to len bytes from fd into buf. Returns bytes read, 0 at
// EOF, or a negative errno on error.
int read(int fd, str buf, int len) {
    return syscall(NR_READ, fd, cast<int>(buf), len);
}

// write: write len bytes from buf to fd. Returns bytes written or a
// negative errno on error.
int write(int fd, str buf, int len) {
    return syscall(NR_WRITE, fd, cast<int>(buf), len);
}

// pread: read len bytes from fd at the given offset, without moving the
// file's read position.
int pread(int fd, str buf, int len, int offset) {
    return syscall(NR_PREAD64, fd, cast<int>(buf), len, offset);
}

// pwrite: write len bytes to fd at the given offset, without moving the
// file's write position.
int pwrite(int fd, str buf, int len, int offset) {
    return syscall(NR_PWRITE64, fd, cast<int>(buf), len, offset);
}

// read_all: read an entire (small/medium) file into a freshly-allocated
// string. Returns nil if the file can't be opened. Not suitable for huge
// files - it grows a buffer and reads until EOF rather than pre-sizing
// from fstat, to avoid needing a real struct stat layout here.
str read_all(str path) {
    int fd = open(path, O_RDONLY, 0);
    if (fd < 0) { return nil; }

    int cap = 65536;
    usize buf = raw_alloc(cap);
    int total = 0;

    while (true) {
        if (total >= cap - 1) {
            int new_cap = cap * 2;
            usize new_buf = raw_alloc(new_cap);
            int i = 0;
            while (i < total) {
                usize sp = buf + cast<usize>(i);
                usize dp = new_buf + cast<usize>(i);
                uint8 c;
                unsafe {
                    *uint8 sbp = cast<*uint8>(sp); c = *sbp;
                    *uint8 dbp = cast<*uint8>(dp); *dbp = c;
                }
                i += 1;
            }
            raw_free(buf, cap);
            buf = new_buf;
            cap = new_cap;
        }

        usize p = buf + cast<usize>(total);
        int n = read(fd, cast<str>(p), cap - total - 1);
        if (n <= 0) { break; }
        total += n;
    }

    close(fd);
    usize term = buf + cast<usize>(total);
    unsafe { *uint8 tp = cast<*uint8>(term); *tp = 0; }
    return cast<str>(buf);
}

// write_all: write an entire string to path, creating/truncating it.
// Returns true on success.
bool write_all(str path, str content) {
    int fd = create(path, 420); // 0644
    if (fd < 0) { return false; }
    int len = length(content);
    int n = write(fd, content, len);
    close(fd);
    return n == len;
}

// exists: true if path can be accessed at all (F_OK).
bool exists(str path) {
    return syscall(NR_ACCESS, cast<int>(path), 0) == 0;
}

// mkdir: create a directory. Returns 0 on success, -1 on error.
int mkdir(str path, int mode) {
    return syscall(NR_MKDIR, cast<int>(path), mode);
}

// rmdir: remove an empty directory. Returns 0 on success, -1 on error.
int rmdir(str path) {
    return syscall(NR_RMDIR, cast<int>(path));
}

// remove: remove a file (not a directory). Returns 0 on success, -1 on error.
int remove(str path) {
    return syscall(NR_UNLINK, cast<int>(path));
}

// chmod: change a file's permission bits. Returns 0 on success, -1 on error.
int chmod(str path, int mode) {
    return syscall(NR_CHMOD, cast<int>(path), mode);
}

// chown: change a file's owning user/group. Returns 0 on success, -1 on error.
int chown(str path, int owner_uid, int owner_gid) {
    return syscall(NR_CHOWN, cast<int>(path), owner_uid, owner_gid);
}

// cwd: current working directory.
str cwd() {
    usize buf = raw_alloc(4096);
    int n = syscall(NR_GETCWD, cast<int>(buf), 4096);
    if (n < 0) { raw_free(buf, 4096); return nil; }
    return cast<str>(buf);
}

// chdir: change the current working directory. Returns 0 on success, -1 on error.
int chdir(str path) {
    return syscall(NR_CHDIR, cast<int>(path));
}

// dup: duplicate a file descriptor to the lowest available fd.
int dup(int fd) {
    return syscall(NR_DUP, fd);
}

// dup2: duplicate oldfd onto newfd, closing newfd first if it was open.
int dup2(int oldfd, int newfd) {
    return syscall(NR_DUP2, oldfd, newfd);
}

// pipe: create a pipe. Writes the read/write fds through the two pointer
// out-parameters (Hylian doesn't have multi-return yet).
int pipe(usize read_fd_out, usize write_fd_out) {
    usize fds = raw_alloc(8); // two 4-byte fds, contiguous, matches int[2]
    int rc = syscall(NR_PIPE, cast<int>(fds));
    if (rc == 0) {
        unsafe {
            *int rp = cast<*int>(read_fd_out);
            *int wp = cast<*int>(write_fd_out);
            *int fp0 = cast<*int>(fds);
            *int fp1 = cast<*int>(fds + cast<usize>(4));
            *rp = *fp0;
            *wp = *fp1;
        }
    }
    raw_free(fds, 8);
    return rc;
}
