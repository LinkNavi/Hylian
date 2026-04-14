include(
"core/string",
"core/io",
"core/errors", // just needed for panics
"Game/EnemySpawner", // in the same folder as this file, Game is the project name, no idea how i'd set project names yet 
"Game/Enemies/Goomba", // goomba class in the Enemies folder, includes are reletive to the project root, not the file
)

public class Player{
	Private String name;
	Private Int32 Health;

	Int32 getHealth(){
		return Health;
	}

	(Error?) setHealth(Int32: newHealth){
		if (newHealth.type() != Int32){
			return Err("not an Int32"); //obviously it'd error when you try to pass a non Int32 to the func but just trying to show how errors would work
		}
		Health = newHealth;
	}

// pretend like get and sets are there for the name as well, im lazy

}

(Error?) main(){ // ? just means that it might not return an error, no idea how i'd impl this

Player p;
Goomba g;

Error err = p.setName("Bob");

if (err){
panic(err.message());
}
err = p.setHealth(100);

if (err){
panic(err.message());
}

if (p.getHealth() <= 0){
print("you ded"); //comes from core/io
}

}
