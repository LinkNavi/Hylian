# vendors/

Place vendor packages here. Each package lives in its own subdirectory:

```
vendors/
  sdl2/
    sdl2.hyi    ← native .so wrapper (link + extern declarations)
    sdl2.hy     ← optional pure-Hylian helpers (compiled + cached)
```

Then declare it in `linkle.hy`:

```
vendors {
    sdl2: "vendors/sdl2",
}
```

And include it in your source:

```
include {
    vendors.sdl2,
}
```
