// assert — cheap runtime checks for tests and sanity checks.
//
//   assert(n > 0, "n must be positive");
//   assert_eq(add(2, 3), 5, "add");
//   assert_eq_str(name, "hylian", "name");
//
// A failed check prints "assertion failed: <message>" (plus both values for
// the assert_eq forms) to stderr and exits the process with status 1.
// The message argument is required - Hylian has no way to capture the source
// text of the condition.

include {
    platform.linux_x86_64,
    string,
}

static void _fail(str msg) {
    str line = concat(concat("assertion failed: ", msg), "\n");
    syscall(NR_WRITE, 2, cast<int>(line), length(line));
    syscall(NR_EXIT_GROUP, 1);
}

void assert(bool cond, str msg) {
    if (!cond) { _fail(msg); }
}

void assert_eq(int actual, int expected, str msg) {
    if (actual != expected) {
        _fail("{{msg}}: expected {{expected}}, got {{actual}}");
    }
}

void assert_eq_str(str actual, str expected, str msg) {
    if (!equals(actual, expected)) {
        _fail("{{msg}}: expected \"{{expected}}\", got \"{{actual}}\"");
    }
}

void assert_near(float actual, float expected, float eps, str msg) {
    float d = actual - expected;
    if (d < 0.0) { d = 0.0 - d; }
    if (d > eps) {
        _fail("{{msg}}: expected {{expected}}, got {{actual}}");
    }
}
