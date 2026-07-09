// os.user — user and group identity.

include {
    platform.linux_x86_64,
}

// uid: real user ID of the calling process.
int uid() {
    return syscall(NR_GETUID);
}

// gid: real group ID of the calling process.
int gid() {
    return syscall(NR_GETGID);
}

// euid: effective user ID of the calling process.
int euid() {
    return syscall(NR_GETEUID);
}

// egid: effective group ID of the calling process.
int egid() {
    return syscall(NR_GETEGID);
}

// set_uid: set the real (and effective, if privileged) user ID.
// Returns 0 on success, -1 on error.
int set_uid(int new_uid) {
    return syscall(NR_SETUID, new_uid);
}

// set_gid: set the real (and effective, if privileged) group ID.
// Returns 0 on success, -1 on error.
int set_gid(int new_gid) {
    return syscall(NR_SETGID, new_gid);
}

// is_root: true if the effective user ID is 0.
bool is_root() {
    return euid() == 0;
}
