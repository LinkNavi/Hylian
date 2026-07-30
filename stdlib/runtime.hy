// runtime — low-level primitives the compiler calls directly for the
// print/println *syntax* (not the std.io print()/println() functions,
// which are a separate, ordinary stdlib layer built on top of syscall()
// and never actually invoked by print/println source - see
// compiler/ir_to_mir.c's IR_PRINT/IR_PRINTLN case, which lowers straight
// to calls named exactly "hylian_print"/"hylian_println" regardless of
// whether std.io is even included).
//
// Always-linked, same reasoning and same constraints as mem.hy: needed by
// every compiled program unconditionally, so it's compiled standalone and
// linked in separately (see linkle.py's STD_MODULES __always__ handling),
// and deliberately does NOT `include platform.linux_x86_64` to avoid a
// multiple-definition clash with whatever the program's own translation
// unit independently compiles from that file.
//
// `strlen` (used by IR_PRINTLN's PRINT_ARG_STR_PTR path, for printing a
// runtime str variable rather than a literal) isn't defined here - it
// isn't needed to be: Hylian binaries currently always link against gcc's
// default (non-freestanding) startup, which pulls in glibc, and glibc
// already provides a standard `strlen`.
//
// hylian_println writes its trailing newline from a 1-byte local instead
// of a `"\n"` string literal: the compiler never processes backslash
// escapes in string literals anywhere (compiler/lower.c's NODE_LITERAL
// only strips the surrounding quotes) - so `"\n"` is actually the two raw
// bytes '\' and 'n', not a newline. Storing the byte value 10 in a local
// and writing 1 byte from its address sidesteps that entirely (this
// works because Hylian locals are 8-byte slots and x86 is little-endian,
// so the value's low byte lands at the start of the slot's address).

naked void hylian_print(usize str_ptr, usize len) {
    syscall(1, 1, cast<int>(str_ptr), cast<int>(len));
}

naked void hylian_println(usize str_ptr, usize len) {
    syscall(1, 1, cast<int>(str_ptr), cast<int>(len));
    usize newline = cast<usize>(10);
    usize newline_addr = cast<usize>(&newline);
    syscall(1, 1, cast<int>(newline_addr), 1);
}
