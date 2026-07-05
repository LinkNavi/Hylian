project {
    name: "Linkle",
    version: "0.1.0",
    author: "",
}

build {
    src: "src",
    main: "main",
    out: "build",
    bin: "Linkle",
}

// Declare native or Hylian vendor packages here.
// vendors {
//     sdl2: "vendors/sdl2",
// }

// Declare registry packages here.
// packages {
//     mylib: "1.0.0",
// }

target run() {
    exec("./build/bin/Linkle");
}

target clean() {
    exec("rm -rf build");
}
