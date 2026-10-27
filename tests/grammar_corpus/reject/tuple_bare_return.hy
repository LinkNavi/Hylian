// A tuple needs parentheses: `return a / b, a % b;` is not a tuple.
(int, int) divmod(int a, int b) {
    return a / b, a % b;
}
