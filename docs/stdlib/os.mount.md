# `os.mount`

```hylian
include { os.mount, }
```

Source: `stdlib/os/mount.hy`. Filesystem mounting and system power control. Linux-only
and mostly meaningful for init-system-style code running as PID 1 or with root
privileges.

| Function | Signature | Description |
|---|---|---|
| `mount` | `int mount(str source, str target, str fstype, int flags, str data)` | Mount a filesystem. `source` is a device path or `"none"` for pseudo-filesystems; `target` must already exist; `flags` are `MS_*` (from `platform.linux_x86_64`); `data` (e.g. `"mode=0755"`) may be `nil`. |
| `unmount` | `int unmount(str target, int flags)` | Unmount. `flags = 1` forces it (`MNT_FORCE`). |
| `reboot` | `void reboot()` | Restart the system immediately. Does not return on success. |
| `poweroff` | `void poweroff()` | Power off immediately. Does not return on success. |
| `halt` | `void halt()` | Halt the CPU without powering off. Does not return on success. |
| `enable_cad` | `void enable_cad()` | Make Ctrl-Alt-Del trigger an immediate reboot. |
| `disable_cad` | `void disable_cad()` | Make Ctrl-Alt-Del send `SIGINT` to PID 1 instead of rebooting. |

```hylian
include { os.mount, platform.linux_x86_64, }

int main() {
    return mount("proc", "/proc", "proc", 0, nil);
}
```
