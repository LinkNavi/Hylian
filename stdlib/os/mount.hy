// os.mount — filesystem mounting and system power control.
// These are Linux-only and mostly meaningful for init-system-style code
// running as PID 1 or with root privileges.

include {
    platform.linux_x86_64,
}

// mount: mount a filesystem.
//   source — device path (e.g. "/dev/sda1"), or "none" for pseudo-filesystems
//   target — mount point (must already exist)
//   fstype — filesystem type string (e.g. "ext4", "proc", "sysfs", "tmpfs")
//   flags  — MS_* flags (MS_RDONLY, MS_NOSUID, MS_NODEV, MS_NOEXEC)
//   data   — options string (e.g. "mode=0755"), may be nil
// Returns 0 on success, -1 on error.
int mount(str source, str target, str fstype, int flags, str data) {
    return syscall(NR_MOUNT, cast<int>(source), cast<int>(target),
                   cast<int>(fstype), flags, cast<int>(data));
}

// unmount: unmount a filesystem. flags=1 forces it (MNT_FORCE).
// Returns 0 on success, -1 on error.
int unmount(str target, int flags) {
    return syscall(NR_UMOUNT2, cast<int>(target), flags);
}

// reboot: restart the system immediately. Does not return on success.
void reboot() {
    syscall(NR_REBOOT, REBOOT_MAGIC1, REBOOT_MAGIC2, REBOOT_CMD_RESTART, 0);
}

// poweroff: power off the system immediately. Does not return on success.
void poweroff() {
    syscall(NR_REBOOT, REBOOT_MAGIC1, REBOOT_MAGIC2, REBOOT_CMD_POWER_OFF, 0);
}

// halt: halt the CPU without powering off. Does not return on success.
void halt() {
    syscall(NR_REBOOT, REBOOT_MAGIC1, REBOOT_MAGIC2, REBOOT_CMD_HALT, 0);
}

// enable_cad: make Ctrl-Alt-Del trigger an immediate reboot.
void enable_cad() {
    syscall(NR_REBOOT, REBOOT_MAGIC1, REBOOT_MAGIC2A, REBOOT_CMD_CAD_ON, 0);
}

// disable_cad: make Ctrl-Alt-Del send SIGINT to PID 1 instead of rebooting.
void disable_cad() {
    syscall(NR_REBOOT, REBOOT_MAGIC1, REBOOT_MAGIC2A, REBOOT_CMD_CAD_OFF, 0);
}
