include {
    std.io,
    shapes,
    vendors.gfx,
}

int main() {
    Circle c = new Circle();
    c.radius = 4;
    Vec2 mouse = GetMousePos();
    int total = add(1, 2);
    return total;
}
