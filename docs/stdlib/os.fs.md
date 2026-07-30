# `os.fs`

```hylian
include { os.fs, }
```

Source: `stdlib/os/fs.hy`. Filesystem operations over raw syscalls.

| Function | Signature | Description |
|---|---|---|
| `open` | `int open(str path, int flags, int mode)` | Open a file. Returns a fd or a negative errno. |
| `create` | `int create(str path, int mode)` | Convenience wrapper: open for writing, creating/truncating (`O_WRONLY \| O_CREAT \| O_TRUNC`). |
| `close` | `void close(int fd)` | Close a file descriptor. |
| `read` | `int read(int fd, str buf, int len)` | Read up to `len` bytes into `buf`. Returns bytes read, `0` at EOF, or a negative errno. |
| `write` | `int write(int fd, str buf, int len)` | Write `len` bytes from `buf`. Returns bytes written or a negative errno. |
| `pread` | `int pread(int fd, str buf, int len, int offset)` | Read at `offset` without moving the file position. |
| `pwrite` | `int pwrite(int fd, str buf, int len, int offset)` | Write at `offset` without moving the file position. |
| `read_all` | `str read_all(str path)` | Read an entire small/medium file into a freshly-allocated string. Returns `nil` if the file can't be opened. Grows a buffer and reads to EOF (no `fstat`-based presizing), so it isn't meant for huge files. |
| `write_all` | `bool write_all(str path, str content)` | Write `content` to `path`, creating/truncating it. Returns `true` on success. |
| `exists` | `bool exists(str path)` | True if `path` can be accessed at all (`F_OK`). |
| `mkdir` | `int mkdir(str path, int mode)` | Create a directory. |
| `rmdir` | `int rmdir(str path)` | Remove an empty directory. |
| `remove` | `int remove(str path)` | Remove a file (not a directory). |
| `chmod` | `int chmod(str path, int mode)` | Change permission bits. |
| `chown` | `int chown(str path, int owner_uid, int owner_gid)` | Change owning user/group. |
| `cwd` | `str cwd()` | Current working directory. |
| `chdir` | `int chdir(str path)` | Change the current working directory. |
| `dup` | `int dup(int fd)` | Duplicate a fd to the lowest available fd. |
| `dup2` | `int dup2(int oldfd, int newfd)` | Duplicate `oldfd` onto `newfd`, closing `newfd` first if open. |
| `pipe` | `int pipe(usize read_fd_out, usize write_fd_out)` | Create a pipe. Since Hylian has no multi-return, the read/write fds come back through the two pointer out-parameters. |

```hylian
include { os.fs, io, }

int main() {
    if (!write_all("out.txt", "hello")) {
        return 1;
    }
    str contents = read_all("out.txt");
    if (contents == nil) {
        return 1;
    }
    return 0;
}
```
