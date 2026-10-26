include { std.io, }

// `==` / `!=` on strings compare bytes, and must work in a program that does
// NOT include std.string (it used to call std.string's `equals`, an undefined
// symbol at link time unless something else pulled that module in).
int main() {
    str a = "ab" + "c";
    str b = "abc";
    str c = "abd";
    str empty = "";
    str e2 = "";
    println("streq: {{a == b}} {{a != b}} {{a == c}} {{a != c}} {{empty == e2}}");
    if (a == b) { println("if-equal"); }
    if (a != c) { println("if-differ"); }
    return 0;
}
