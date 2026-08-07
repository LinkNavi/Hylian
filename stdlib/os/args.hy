// os.args — command-line arguments, shaped like Go's os.Args.
//
// Go:
//     argc := len(os.Args)
//     fmt.Printf("Program name: %s\n", os.Args[0])
//     for i, arg := range os.Args[1:] { ... }
//
// Hylian:
//     include { std.io, std.os.args, }
//
//     void main() {
//         array<str> Args = args();
//         println(Args.len);              // len(os.Args)
//         println(Args[0]);               // os.Args[0]
//
//         if (Args.len > 1) {
//             int i = 1;
//             for (arg in args_tail()) {  // os.Args[1:]
//                 println(i);
//                 println(arg);
//                 i = i + 1;
//             }
//         }
//     }
//
// args() is the whole slice, program name at index 0, exactly like os.Args;
// args_tail() is the os.Args[1:] half. Both are ordinary array<str> values, so
// `.len` and `[i]` work as they do everywhere else rather than needing special
// accessors. argc(), argv(i) and program_name() are there for convenience.
//
// Two ways the arguments get here:
//
// 1. Your main() declares them. Hylian binaries are started by the C runtime,
//    which calls main(argc, argv) in the ordinary SysV way, so a main with
//    parameters simply receives them — zero syscalls, no allocation beyond the
//    array itself:
//
//        int main(int argc, usize argv) {
//            array<str> Args = args_from(argc, argv);
//            return 0;
//        }
//
// 2. Your main() doesn't. args() then reads /proc/self/cmdline, so it works
//    from anywhere in the program without threading argc/argv through every
//    call. The parse is cached, so repeated calls cost nothing.

include {
    platform.linux_x86_64,
    string,
}

// Cached result of the /proc/self/cmdline parse. nil until args() is first
// called; the buffer it points into is deliberately never freed, because the
// strs in the returned array point directly at it.
static usize _ARGS_CACHE = 0;

// args_from: build an array<str> from the argc/argv a main() with parameters
// receives. argv is the raw char** the C runtime handed over.
array<str> args_from(int argc, usize argv) {
    array<str> out = [];
    if (argv == cast<usize>(0)) { return out; }

    int i = 0;
    while (i < argc) {
        usize slot = argv + cast<usize>(i * 8);
        str s;
        unsafe {
            *usize p = cast<*usize>(slot);
            s = cast<str>(*p);
        }
        out.push(s);
        i += 1;
    }
    return out;
}

// _read_cmdline: the raw /proc/self/cmdline block, NUL-separated and
// NUL-terminated, or 0 if it can't be read.
static usize _read_cmdline() {
    if (_ARGS_CACHE != cast<usize>(0)) { return _ARGS_CACHE; }

    str proc_path = "/proc/self/cmdline";
    int fd = syscall(NR_OPEN, cast<int>(proc_path), O_RDONLY, 0);
    if (fd < 0) { return cast<usize>(0); }

    // ARG_MAX is 2MB on Linux but a command line that large is pathological;
    // 128KB covers anything realistic and is a single allocation.
    int cap = 131072;
    usize buf = raw_alloc(cap);
    int n = syscall(NR_READ, fd, cast<int>(buf), cap - 2);
    syscall(NR_CLOSE, fd, 0, 0);

    if (n <= 0) {
        raw_free(buf, cap);
        return cast<usize>(0);
    }

    // Guarantee two trailing NULs so the scan below always terminates, even if
    // the kernel handed back an unterminated final entry.
    unsafe {
        *uint8 e0 = cast<*uint8>(buf + cast<usize>(n));
        *e0 = cast<uint8>(0);
        *uint8 e1 = cast<*uint8>(buf + cast<usize>(n + 1));
        *e1 = cast<uint8>(0);
    }

    _ARGS_CACHE = buf;
    return buf;
}

// args: every command-line argument, argv[0] first. Empty if unavailable.
//
// The entries point straight into the cached /proc block rather than being
// copied — each one is already NUL-terminated in place, which is exactly the
// representation a `str` needs.
array<str> args() {
    array<str> out = [];
    usize buf = _read_cmdline();
    if (buf == cast<usize>(0)) { return out; }

    int i = 0;
    int start = 0;
    bool done = false;
    while (!done) {
        uint8 c;
        unsafe {
            *uint8 p = cast<*uint8>(buf + cast<usize>(i));
            c = *p;
        }

        if (c == cast<uint8>(0)) {
            if (i == start) {
                // an empty entry at this position means end of the block
                done = true;
            } else {
                out.push(cast<str>(buf + cast<usize>(start)));
                start = i + 1;
            }
        }
        i += 1;
    }
    return out;
}

// args_tail: the arguments without argv[0] — the ones a user actually typed.
array<str> args_tail() {
    array<str> all = args();
    array<str> out = [];
    int i = 1;
    while (i < all.len) {
        out.push(all[i]);
        i += 1;
    }
    return out;
}

// argc: number of command-line arguments, including argv[0].
int argc() {
    return args().len;
}

// argv: the argument at `i`, or nil if out of range.
str argv(int i) {
    array<str> a = args();
    if (i < 0) { return nil; }
    if (i >= a.len) { return nil; }
    return a[i];
}

// program_name: argv[0] as invoked, or nil.
str program_name() {
    return argv(0);
}

// has_flag: true if any argument after argv[0] equals `flag` exactly.
// The common "did they pass --verbose" case, without a loop at every call.
bool has_flag(str flag) {
    array<str> a = args();
    int i = 1;
    while (i < a.len) {
        if (equals(a[i], flag)) { return true; }
        i += 1;
    }
    return false;
}

// flag_value: the argument following `flag`, or nil if absent or last.
// Handles the "--out path" shape, not "--out=path".
str flag_value(str flag) {
    array<str> a = args();
    int i = 1;
    while (i < a.len) {
        if (equals(a[i], flag)) {
            if (i + 1 < a.len) { return a[i + 1]; }
            return nil;
        }
        i += 1;
    }
    return nil;
}
