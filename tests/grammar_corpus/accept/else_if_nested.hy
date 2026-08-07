void nested(int a, int b) {
    if (a > 0) {
        if (b > 0) {
            println("both");
        } else if (b == 0) {
            println("b zero");
        }
    } else if (a == 0) {
        println("a zero");
    }
}
