// time — wall-clock time and sleep.

include {
    platform.linux_x86_64,
}

// struct timespec, exactly as the kernel expects it: two 64-bit words.
//
// This used to be a raw_alloc(16) with the two fields poked in and read back
// through hand-written pointer arithmetic, because struct field access didn't
// work — every `obj.field` compiled to a call to a `<Class>_get_<field>`
// function that nothing generated, so it failed at link time. Now that fields
// lower to a direct load/store at a computed offset, the struct can just be a
// struct, and the layout is checked by the compiler instead of by counting
// bytes in a comment.
//
// `packed` so the two words stay exactly 16 bytes with nothing inserted —
// this is handed straight to the kernel, so its layout is not ours to change.
packed class Timespec {
    int64 sec;
    int64 nsec;
}

// now: current wall-clock time in whole seconds since the Unix epoch.
int now() {
    Timespec ts;
    syscall(NR_CLOCK_GETTIME, 0, cast<int>(&ts));
    return cast<int>(ts.sec);
}

// nanos: current wall-clock time in nanoseconds since the Unix epoch.
int nanos() {
    Timespec ts;
    syscall(NR_CLOCK_GETTIME, 0, cast<int>(&ts));
    return cast<int>(ts.sec) * 1000000000 + cast<int>(ts.nsec);
}

// millis: current wall-clock time in milliseconds since the Unix epoch -
// the granularity most application code actually wants.
int millis() {
    return nanos() / 1000000;
}

// sleep: pause the calling thread for the given number of milliseconds.
void sleep(int ms) {
    Timespec req;
    req.sec = cast<int64>(ms / 1000);
    req.nsec = cast<int64>((ms % 1000) * 1000000);
    syscall(NR_NANOSLEEP, cast<int>(&req), 0);
}

// sleep_ns: pause for a number of nanoseconds, for callers that need finer
// granularity than sleep()'s milliseconds.
void sleep_ns(int seconds, int nanoseconds) {
    Timespec req;
    req.sec = cast<int64>(seconds);
    req.nsec = cast<int64>(nanoseconds);
    syscall(NR_NANOSLEEP, cast<int>(&req), 0);
}
