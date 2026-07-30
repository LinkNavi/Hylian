# `os.user`

```hylian
include { os.user, }
```

Source: `stdlib/os/user.hy`. User and group identity.

| Function | Signature | Description |
|---|---|---|
| `uid` | `int uid()` | Real user ID of the calling process. |
| `gid` | `int gid()` | Real group ID. |
| `euid` | `int euid()` | Effective user ID. |
| `egid` | `int egid()` | Effective group ID. |
| `set_uid` | `int set_uid(int new_uid)` | Set the real (and effective, if privileged) user ID. |
| `set_gid` | `int set_gid(int new_gid)` | Set the real (and effective, if privileged) group ID. |
| `is_root` | `bool is_root()` | True if the effective user ID is `0`. |

```hylian
include { os.user, io, }

int main() {
    if (is_root()) {
        println("running as root");
    }
    return 0;
}
```
