# `platform.linux_x86_64`

```hylian
include { platform.linux_x86_64, }
```

Source: `stdlib/platform/linux_x86_64.hy`. Linux/x86-64 syscall numbers and flag
constants — the **only** platform file as of v0.1.0 (Hylian currently targets
Linux/x86-64 exclusively). Every module above this one (`time`, `os.*`, `io`, `net`,
`string`) calls the `syscall(nr, ...)` compiler builtin directly using the constant
names defined here — none of them contain a raw number or know they're "on Linux". A
future port (another OS, another architecture) means adding a sibling file with the
same constant names but different values; nothing above this file should need to
change.

## `raw_alloc` / `raw_free`

The one pair of actual functions in this file — every module that needs scratch memory
(argv arrays, path buffers, socket address structs, timespec buffers, ...) goes through
these instead of each calling `mmap` directly, so the real `syscall(NR_MMAP, ...)`
argument order lives in exactly one place:

| Function | Signature | Description |
|---|---|---|
| `raw_alloc` | `usize raw_alloc(int size)` | `mmap` an anonymous, private, read/write region of `size` bytes. |
| `raw_free` | `void raw_free(usize ptr, int size)` | `munmap` it. |

## Constants

Syscall numbers (`NR_READ`, `NR_WRITE`, `NR_OPEN`, `NR_CLOSE`, `NR_MMAP`, `NR_MUNMAP`,
`NR_SOCKET`, `NR_CONNECT`, `NR_FORK`, `NR_EXECVE`, `NR_MOUNT`, `NR_REBOOT`, and the rest
of the set every other stdlib module calls through), plus flag groups:

| Group | Constants |
|---|---|
| mmap prot/flags | `PROT_READ`, `PROT_WRITE`, `PROT_EXEC`, `MAP_PRIVATE`, `MAP_ANONYMOUS` |
| `open()` flags | `O_RDONLY`, `O_WRONLY`, `O_RDWR`, `O_CREAT`, `O_TRUNC`, `O_APPEND` |
| `mount()` flags | `MS_RDONLY`, `MS_NOSUID`, `MS_NODEV`, `MS_NOEXEC` |
| `reboot()` magic/commands | `REBOOT_MAGIC1`, `REBOOT_MAGIC2`, `REBOOT_MAGIC2A`, `REBOOT_CMD_RESTART`, `REBOOT_CMD_HALT`, `REBOOT_CMD_POWER_OFF`, `REBOOT_CMD_CAD_ON`, `REBOOT_CMD_CAD_OFF` |
| socket family/type | `AF_INET`, `SOCK_STREAM`, `SOCK_DGRAM` |

See `stdlib/platform/linux_x86_64.hy` directly for the full syscall-number list and
exact values — it's the single source of truth other modules are written against, and
is short enough to read in one pass.
