// enums with implicit and explicit values, used as constants and in switch
enum Status {
    Ok,
    Warning,
    Failure,
}

enum Level {
    Low = 10,
    Medium = 20,
    High = 30,
}

void classify(int v) {
    if (v == Status.Ok) {
        println("ok");
    }
    switch (v) {
        case Level.Low:  { println("low"); }
        case Level.High: { println("high"); }
        default:         { println("other"); }
    }
}
