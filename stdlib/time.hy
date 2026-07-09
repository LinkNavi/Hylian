// time — wall-clock time and sleep.

include {
    platform.linux_x86_64,
}

// struct timespec { int64 tv_sec; int64 tv_nsec; } - 16 bytes, laid out
// manually since there's no real struct type wired up yet. Every function
// here that needs one allocates a fresh 16-byte buffer and reads/writes
// the two int64 fields directly via pointer arithmetic.

// now: current wall-clock time in whole seconds since the Unix epoch.
int now() {
    usize ts = raw_alloc(16);
    syscall(NR_CLOCK_GETTIME, 0, cast<int>(ts));
    int sec;
    unsafe { *int p = cast<*int>(ts); sec = *p; }
    raw_free(ts, 16);
    return sec;
}

// nanos: current wall-clock time in nanoseconds since the Unix epoch.
int nanos() {
    usize ts = raw_alloc(16);
    syscall(NR_CLOCK_GETTIME, 0, cast<int>(ts));
    int sec; int nsec;
    unsafe {
        *int ps = cast<*int>(ts); sec = *ps;
        *int pn = cast<*int>(ts + cast<usize>(8)); nsec = *pn;
    }
    raw_free(ts, 16);
    return sec * 1000000000 + nsec;
}

// millis: current wall-clock time in milliseconds since the Unix epoch -
// the granularity most application code actually wants.
int millis() {
    return nanos() / 1000000;
}

// sleep: pause the calling thread for the given number of milliseconds.
void sleep(int ms) {
    usize req = raw_alloc(16);
    unsafe {
        *int ps = cast<*int>(req); *ps = ms / 1000;
        *int pn = cast<*int>(req + cast<usize>(8)); *pn = (ms % 1000) * 1000000;
    }
    syscall(NR_NANOSLEEP, cast<int>(req), 0);
    raw_free(req, 16);
}
