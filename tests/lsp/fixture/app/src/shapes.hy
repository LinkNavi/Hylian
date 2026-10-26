// A circle you can grow.
class Circle {
    public int radius;
    public str label;
    private int secret;

    // Area in square units (integer approximation).
    int area() {
        return radius * radius * 3;
    }

    void grow(int by) {
        radius += by;
    }
}

enum Color { Red, Green, Blue }

// Add two numbers.
int add(int a, int b) {
    return a + b;
}
