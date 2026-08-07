const int LIMIT = 10;

enum Colour {
    Red,
    Green,
}

class Point {
    int x;
    int y;
}

int add(int a, int b) {
    return a + b;
}

void loops() {
    int i = 0;
    while (i < LIMIT) {
        i = i + 1;
    }
    for (int j = 0; j < LIMIT; j = j + 1) {
        if (j == 3) { continue; }
        if (j == 7) { break; }
    }
}

void switching(int v) {
    switch (v) {
        case 1: { println("a"); }
        case 2: { println("b"); }
        default: { println("c"); }
    }
}
