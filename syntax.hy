include {
    core.string,
    core.io,
    core.errors,
    Game.EnemySpawner,
    Game.Enemies.Goomba,
}

public class Player {
    private str name;
    private int health;

    int getHealth() {
        return health;
    }

    Error? setHealth(int newHealth) {
        if (newHealth < 0) {
            return Err("health cannot be negative");
        }
        health = newHealth;
        return nil;
    }
}

Error? main() {
    Player p = new Player();
    Goomba g = new Goomba();

    err := p.setName("Bob");
    if (err) {
        panic(err.message());
    }

    err = p.setHealth(100);
    if (err) {
        panic(err.message());
    }

    if (p.getHealth() <= 0) {
        print("you ded");
    }

    // typed array - flexible size, maps to std::vector<Enemy>
    array<Enemy> enemies = [g, otherEnemy];

    // typed array - fixed size, maps to std::array<Enemy, 5>
    array<Enemy, 5> fiveEnemies = [g, g, g, g, g];

    // multi - union types, flexible size, maps to std::vector<std::variant<str, int>>
    multi<str | int> objects = ["hello", 42, "world"];

    // multi - union types, fixed size, maps to std::array<std::variant<str, int>, 3>
    multi<str | int, 3> threeThings = ["hello", 42, "world"];

    // multi<any> - any type, flexible size, maps to std::vector<std::any>
    multi<any> stuff = ["hello", 42, true, 3.14];

    // indexing works on both
    int first = enemies[0];
    enemies[1] = g;

    return nil;
}
