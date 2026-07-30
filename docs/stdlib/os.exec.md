# `os.exec`

```hylian
include { os.exec, }
```

Source: `stdlib/os/exec.hy`. Process creation and control.

| Function | Signature | Description |
|---|---|---|
| `fork` | `int fork()` | Create a child process. Returns `0` in the child, the child's PID in the parent, or `-1` on error. |
| `pid` | `int pid()` | The calling process's own PID. |
| `kill` | `int kill(int target_pid, int sig)` | Send a signal to a process. |
| `wait` | `int wait(int target_pid)` | Block until `target_pid` exits, returning its exit status. `-1` waits for any child. |
| `exec` | `int exec(str path, usize args_ptrs, int arg_count, usize envp_ptrs)` | Replace the current process image. Does not return on success. `envp_ptrs = 0` passes an empty environment. |
| `spawn` | `int spawn(str path, usize args_ptrs, int arg_count)` | `fork` + `exec` + `wait` in one call — run a program and block until it finishes. Returns the child's exit status, or `-1` if fork/exec itself failed. |

`args_ptrs` for `exec`/`spawn` is the address of an existing array of `str` values in
memory (`arg_count` of them, 8 bytes each, **not including** `argv[0]`, which is
`path` itself) — build that buffer with `raw_alloc` yourself, the same way you'd build
any other raw buffer, since there's no `array<str>`-to-C-argv conversion built in.
Internally, `exec` packs `path` plus that buffer into the null-terminated
`char**`-shaped layout `execve` expects via a private `_build_argv` helper.

```hylian
include { os.exec, platform.linux_x86_64, }

int main() {
    return spawn("/bin/ls", raw_alloc(0), 0);
}
```
