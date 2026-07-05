include {
    std.sys,
}

// ── init system helpers ──────────────────────────────────────────────────────
// Mount / power-control primitives for building an init system.
// All of these are Linux-only — they wrap the corresponding syscalls.

// mount: mount a filesystem.
//   source  — device path (e.g. "/dev/sda1"), or "none" for pseudo-filesystems
//   target  — mount point (must already exist)
//   fstype  — filesystem type string (e.g. "ext4", "proc", "sysfs", "tmpfs")
//   flags   — MS_* flags (e.g. MS_NOSUID=2, MS_NODEV=4, MS_NOEXEC=8, MS_RDONLY=1)
//   data    — options string (e.g. "mode=0755"), may be nil
// Returns 0 on success, -1 on error.
@target(linux)
int mount(str source, str target, str fstype, int flags, str data) {
    return sys_mount(source, target, fstype, flags, data);
}

// umount: unmount a filesystem (force = MNT_FORCE = 1).
// Returns 0 on success, -1 on error.
@target(linux)
int umount(str target, int flags) {
    return sys_umount2(target, flags);
}

// ── power controls ───────────────────────────────────────────────────────────

// reboot: restart the system. Does not return on success.
@target(linux)
void reboot() {
    sys_reboot(_LINUX_REBOOT_MAGIC1, _LINUX_REBOOT_MAGIC2,
               _LINUX_REBOOT_CMD_RESTART, 0);
}

// poweroff: power off the system. Does not return on success.
@target(linux)
void poweroff() {
    sys_reboot(_LINUX_REBOOT_MAGIC1, _LINUX_REBOOT_MAGIC2,
               _LINUX_REBOOT_CMD_POWER_OFF, 0);
}

// halt: halt the CPU (stop execution, do not power off). Does not return.
@target(linux)
void halt() {
    sys_reboot(_LINUX_REBOOT_MAGIC1, _LINUX_REBOOT_MAGIC2,
               _LINUX_REBOOT_CMD_HALT, 0);
}

// enable_cad: enable Ctrl-Alt-Del to trigger an immediate reboot.
@target(linux)
void enable_cad() {
    sys_reboot(_LINUX_REBOOT_MAGIC1, _LINUX_REBOOT_MAGIC2A,
               _LINUX_REBOOT_CMD_CAD_ON, 0);
}

// disable_cad: disable Ctrl-Alt-Del (SIGHUP to init instead).
@target(linux)
void disable_cad() {
    sys_reboot(_LINUX_REBOOT_MAGIC1, _LINUX_REBOOT_MAGIC2A,
               _LINUX_REBOOT_CMD_CAD_OFF, 0);
}
