// Tuples were removed from the language. Both grammars must reject this;
// the tree-sitter grammar used to accept it, which is the drift this catches.
(int, int) divmod(int a, int b) {
    return a / b, a % b;
}
