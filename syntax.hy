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
    str getName() {
        return name;
    }
    int getHealth() {
        return health;
    }
    Error? setName(str newName) {
        if (newName.length() == 0) {
            return Err("name cannot be empty");
        }
        name = newName;
        return nil;
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
    Error? err = p.setName("Bob");
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
    array<Enemy> enemies = [g];
    array<Enemy, 5> fiveEnemies = [g, g, g, g, g];
    multi<str | int> objects = ["hello", 42, "world"];
    multi<str | int, 3> threeThings = ["hello", 42, "world"];
    multi<any> stuff = ["hello", 42, true, 3.14];
    Enemy first = enemies[0];
    enemies[1] = g;
    return nil;
}
