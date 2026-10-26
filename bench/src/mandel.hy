include {
    std.io,
}
Error? main() {
    int total = 0;
    for (int py = 0; py < 600; py++) {
        for (int px = 0; px < 600; px++) {
            float cr = cast<float>(px) * 3.0 / 600.0 - 2.0;
            float ci = cast<float>(py) * 2.0 / 600.0 - 1.0;
            float zr = 0.0;
            float zi = 0.0;
            int it = 0;
            while (it < 100 && zr * zr + zi * zi < 4.0) {
                float t = zr * zr - zi * zi + cr;
                zi = 2.0 * zr * zi + ci;
                zr = t;
                it += 1;
            }
            total += it;
        }
    }
    println(total);
    return nil;
}
