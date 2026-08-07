// Regression: the LSP grammar had no `else if` rule, so this file compiled and
// ran correctly while the editor reported "unexpected IF, expecting LBRACE".
void classify(int x) {
    if (x == 1) {
        println("one");
    } else if (x == 2) {
        println("two");
    } else if (x == 3) {
        println("three");
    } else {
        println("many");
    }
}
