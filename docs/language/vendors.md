# Vendor Packages and Native FFI

The vendor system lets you wrap native shared libraries and distribute pure-Hylian helper code as self-contained packages inside your project. Vendor packages live in the `vendors/` directory and are declared in `linkle.hy`. Once declared, they are imported with the same `include` syntax used for the standard library.

---

## Project Layout

```
my-project/
├── linkle.hy
├── src/
│   └── main.hy
├── vendors/
│   └── mylib/
│       ├── mylib.hyi      ← native .so wrapper: link directive + extern declarations
│       └── mylib.hy       ← optional pure-Hylian helpers, compiled and cached
└── build/
    ├── bin/
    │   └── my-project
    └── obj/
        ├── main.asm
        ├── main.o
        └── vendors/
            └── mylib.o    ← cached vendor object file
```

Each package lives in its own subdirectory under `vendors/`. The subdirectory name is the package name. At minimum a package contains a `.hyi` interface file. A `.hy` file is optional and is compiled and linked alongside your source if present.

---

## The `.hyi` Interface File

A `.hyi` file is the public interface declaration for a vendor package. It tells the compiler what symbols the package exports and which shared library to link against. The format is similar to a Hylian source file, but contains only declarations — no function bodies.

### Module Declaration

Every `.hyi` file must begin with a `module` declaration. This sets the include path that Hylian source files use to import the package.

```
module vendors.mylib
```

The module name must match the pattern `vendors.<packagename>`.

### Link Directive

The `link` directive names the shared library the linker should pass to `gcc`. This is the soname of the `.so`, not a file path.

```
link "libmylib.so.1"
```

Multiple `link` directives are allowed if the package wraps more than one library:

```
link "libfoo.so.2"
link "libbar.so.1"
```

### Function Declarations

Free functions are declared with `fn`. Parameter types and return types use standard Hylian type names.

```
fn myFunc(arg: int) -> str
fn anotherFunc(a: str, b: float) -> int
fn noReturn(msg: str)
```

### Class Declarations

Classes map to opaque native handle types. The class body contains only method declarations. The runtime representation of the class is determined by the native library — Hylian treats it as an opaque pointer.

```
class MyHandle {
    fn create(arg: str) -> MyHandle
    fn destroy()
    fn doThing(x: int) -> int
    fn getName() -> str
}
```

Constructors are declared as static-style methods that return the class type (commonly named `create` or `init`). A destructor is typically a `destroy` or `free` method. Neither is enforced by the language — the convention matches whatever the native library provides.

### Complete `.hyi` Example

```
// Hylian vendor interface — vendors.mylib
module vendors.mylib
link "libmylib.so.1"

// Free functions
fn mylib_version() -> str
fn mylib_init(flags: int) -> int

// Opaque handle type
class MyHandle {
    fn create(name: str) -> MyHandle
    fn destroy()
    fn process(data: str, len: int) -> int
    fn get_result(out_buf: str) -> int
    fn set_option(key: str, value: str) -> int
}
```

---

### Struct Declarations

Structs map to plain C structs whose fields you read and write directly. Unlike opaque handles (which use `class`), a struct has no constructor and no methods — it is a flat block of memory you fill in and pass by pointer to native functions.

```
struct VkInstanceCreateInfo {
    int32  sType
    ptr    pNext
    int32  flags
    int32  enabledLayerCount
    ptr    ppEnabledLayerNames
    int32  enabledExtensionCount
    ptr    ppEnabledExtensionNames
}
```

#### Field Types

| `.hyi` type | C equivalent | Size |
|---|---|---|
| `int8` / `uint8` | `int8_t` / `uint8_t` | 1 byte |
| `int16` / `uint16` | `int16_t` / `uint16_t` | 2 bytes |
| `int32` / `uint32` | `int32_t` / `uint32_t` | 4 bytes |
| `float32` | `float` | 4 bytes |
| `int64` / `uint64` | `int64_t` / `uint64_t` | 8 bytes |
| `int` / `float` | `int64_t` / `double` | 8 bytes |
| `ptr` / `rawptr` | `void*` | 8 bytes |
| `str` | `char*` | 8 bytes |
| `TypeName` | another struct/handle | 8 bytes |

#### `class` vs `struct` in `.hyi`

| | `class` | `struct` |
|---|---|---|
| Purpose | Opaque handle (you pass it around) | Data container (you fill fields) |
| Has constructor | Yes (`new ClassName(...)`) | No (declare bare: `ClassName x;`) |
| Has methods | Yes | No |
| Memory | Heap | Stack |
| Example | `VkInstance`, `VkDevice` | `VkInstanceCreateInfo`, `VkExtent2D` |

---

## Declaring Vendors in `linkle.hy`

Add a `vendors` block to your `linkle.hy` manifest. Each entry maps a package alias to the directory containing the `.hyi` (and optional `.hy`) files. The alias must match the package name used in the `module` declaration.

```
project {
    name: "my-project",
    version: "0.1.0",
    author: "you",
}

build {
    src: "src",
    main: "main",
    out: "build",
    bin: "my-project",
}

vendors {
    mylib: "vendors/mylib",
    vulkan: "vendors/vulkan",
    opengl: "vendors/opengl",
}

target run() {
    exec("./build/bin/my-project");
}

target clean() {
    exec("rm -rf build");
}
```

Each key in the `vendors` block corresponds to `vendors.<key>` as an include path. The value is the path to the package directory relative to the project root.

---

## Using a Vendor Package in Source

Include a vendor package the same way you include any other module:

```hylian
include {
    std.io,
    vendors.mylib,
}

Error? main() {
    int rc = mylib_init(0);
    if (rc < 0) {
        return Err("mylib_init failed");
    }

    MyHandle h = MyHandle.create("example");
    int result = h.process("some data", 9);
    if (result < 0) {
        h.destroy();
        return Err("process failed");
    }

    h.destroy();
    return nil;
}
```

---

## Scaffolding a New Vendor Package

`linkle` can scaffold a new vendor package with the correct directory structure and a commented `.hyi` template:

```sh
linkle vendor new mylib
```

This creates:

```
vendors/
└── mylib/
    └── mylib.hyi
```

The generated `mylib.hyi` contains the full template with commented-out examples for free functions and classes. Fill in the `link` directive and uncomment the declarations that match the library you are wrapping.

You still need to add the entry to the `vendors` block in `linkle.hy` manually:

```
vendors {
    mylib: "vendors/mylib",
}
```

---

## Automatically Generating Bindings with `bindgen.py`

Writing `.hyi` files by hand works well for small libraries, but large C APIs (SDL2, Vulkan, OpenGL, libpng, …) have hundreds of functions and structs. `tools/bindgen.py` automates this by parsing a C header with **libclang** and emitting a ready-to-use `.hyi` file.

### Requirements

```sh
pip install libclang
# On Debian / Ubuntu, also install the native library:
apt install libclang-dev
```

### Basic Usage

```sh
python3 tools/bindgen.py <header> [options]
```

The only required argument is the path to the C header file. All other options are optional.

### Options

| Option | Short | Description |
|---|---|---|
| `--module <name>` | `-m` | Module name to emit (default: `vendors.<stem>`) |
| `--link <lib>` | `-l` | Shared library soname — can be repeated |
| `--out <path>` | `-o` | Output `.hyi` path (default: stdout) |
| `--include <dir>` | `-I` | Extra `-I` flag passed to clang — can be repeated |
| `--filter <prefix>` | `-f` | Only emit symbols whose name starts with this prefix — can be repeated |
| `--opaque <Type>` | | Treat this struct as an opaque class handle — can be repeated |
| `--no-structs` | | Skip all struct declarations |
| `--no-enums` | | Skip all enum → const blocks |
| `--no-functions` | | Skip all function declarations |
| `--transitive` | | Also emit symbols from `#include`d headers (default: main header only) |
| `--with-defines` | | Scrape integer `#define` macros via `gcc -dM -E` and emit as `const` |
| `--verbose` | `-v` | Log skipped / unknown types to stderr |

### Examples

**SDL2 — full binding**

```sh
python3 tools/bindgen.py /usr/include/SDL2/SDL.h \
    --module vendors.sdl2 \
    --link   libSDL2-2.0.so.0 \
    --out    vendors/sdl2/sdl2.hyi
```

**SDL2 — only window and renderer symbols**

```sh
python3 tools/bindgen.py /usr/include/SDL2/SDL.h \
    --module vendors.sdl2 \
    --link   libSDL2-2.0.so.0 \
    --filter SDL_Create --filter SDL_Destroy --filter SDL_Render \
    --out    vendors/sdl2/sdl2.hyi
```

**OpenGL**

```sh
python3 tools/bindgen.py /usr/include/GL/gl.h \
    --module vendors.gl \
    --link   libGL.so.1 \
    --out    vendors/gl/gl.hyi
```

**libpng with an extra include directory**

```sh
python3 tools/bindgen.py /usr/include/png.h \
    --module vendors.png \
    --link   libpng16.so.16 \
    --include /usr/include \
    --out    vendors/png/png.hyi
```

**Opaque struct handles** — SDL_Window, SDL_Renderer, and similar types are opaque pointers in C. Use `--opaque` to tell bindgen to emit them as opaque `class` declarations instead of attempting to expand their fields:

```sh
python3 tools/bindgen.py /usr/include/SDL2/SDL.h \
    --module   vendors.sdl2 \
    --link     libSDL2-2.0.so.0 \
    --opaque   SDL_Window \
    --opaque   SDL_Renderer \
    --opaque   SDL_Texture \
    --out      vendors/sdl2/sdl2.hyi
```

### After Generating

Once the `.hyi` is generated, add the vendor entry to your `linkle.hy` as you normally would:

```
vendors {
    sdl2: "vendors/sdl2",
}
```

The generated file may include symbols you don't need. You can trim it by hand or re-run with `--filter` to narrow the output.

### Working with Large Libraries (SDL2, Vulkan, OpenGL)

Umbrella headers like `SDL.h` pull in hundreds of functions, structs, and enum constants. The LSP completion table has a fixed capacity, so for very large libraries it is recommended to generate a **filtered** `.hyi` containing only the symbols your project actually uses:

```sh
# SDL2 — only the symbols needed for a basic window + renderer app
python3 tools/bindgen.py /usr/include/SDL2/SDL.h \
    --module vendors.sdl2 \
    --link   libSDL2-2.0.so.0 \
    --filter SDL_Init       --filter SDL_Quit \
    --filter SDL_Create     --filter SDL_Destroy \
    --filter SDL_SetRender  --filter SDL_Render \
    --filter SDL_PollEvent  --filter SDL_Delay \
    --filter SDL_GetError \
    --out    vendors/sdl2/sdl2.hyi
```

This produces a compact `.hyi` with only the window, renderer, event, and timing functions rather than the full 800+ function surface area.

**Tip — keep `--with-defines` off for filtered builds.** The `--with-defines` flag scrapes `#define` macros through the preprocessor, which can add hundreds of SDL constant entries. Instead, add only the constants you need as `const` lines in your own `.hy` file:

```hylian
// src/sdl_consts.hy  — hand-picked SDL constants
const int SDL_INIT_VIDEO   = 32;
const int SDL_WINDOW_SHOWN = 4;
const int SDL_QUIT         = 256;
const int SDL_KEYDOWN      = 768;
```

This keeps the LSP snappy and completions focused.

---

## Build Integration

When `linkle build` runs, it:

1. Reads the `vendors` block in `linkle.hy`
2. For each vendor, parses the `.hyi` file to collect `link` directives and symbol declarations
3. Compiles any accompanying `.hy` file and caches the resulting `.o` under `build/obj/vendors/`
4. Passes all `link` directives as `-l` flags to `gcc`
5. Emits `extern` declarations in the generated `.asm` for every symbol the package declares

Vendor object files are cached. A vendor `.hy` is only recompiled when it changes.

---

## Example: Wrapping Vulkan

The Vulkan SDK exposes a C API through `libvulkan.so.1`. Below is a realistic `.hyi` wrapping the core instance and device creation functions needed to get a Vulkan application off the ground.

### `vendors/vulkan/vulkan.hyi`

```
// Hylian vendor interface — vendors.vulkan
// Wraps libvulkan.so.1 (Vulkan SDK, Vulkan-Loader)
module vendors.vulkan
link "libvulkan.so.1"

// ── Create-info structs ───────────────────────────────────────────────────────

// Passed by pointer to vkCreateInstance.
struct VkApplicationInfo {
    int32  sType
    ptr    pNext
    str    pApplicationName
    uint32 applicationVersion
    str    pEngineName
    uint32 engineVersion
    uint32 apiVersion
}

struct VkInstanceCreateInfo {
    int32  sType
    ptr    pNext
    int32  flags
    ptr    pApplicationInfo
    uint32 enabledLayerCount
    ptr    ppEnabledLayerNames
    uint32 enabledExtensionCount
    ptr    ppEnabledExtensionNames
}

struct VkDeviceQueueCreateInfo {
    int32   sType
    ptr     pNext
    int32   flags
    uint32  queueFamilyIndex
    uint32  queueCount
    ptr     pQueuePriorities
}

struct VkDeviceCreateInfo {
    int32   sType
    ptr     pNext
    int32   flags
    uint32  queueCreateInfoCount
    ptr     pQueueCreateInfos
    uint32  enabledLayerCount
    ptr     ppEnabledLayerNames
    uint32  enabledExtensionCount
    ptr     ppEnabledExtensionNames
    ptr     pEnabledFeatures
}

// ── Opaque handles ────────────────────────────────────────────────────────────

// Top-level Vulkan context. Created via vkCreateInstance.
class VkInstance {
    fn destroy()
    fn enumerate_gpus(out_buf: str, out_count: int) -> int
}

// Opaque handle to a physical GPU enumerated from a VkInstance.
class VkPhysicalDevice {
    fn get_name(out_buf: str) -> int
    fn get_queue_family_count() -> int
    fn supports_graphics(queue_family: int) -> int
    fn supports_present(queue_family: int, surface: int) -> int
}

class VkDevice {
    fn destroy()
    fn wait_idle() -> int
    fn get_queue(queue_family: int, index: int) -> int
}

// ── Swapchain ─────────────────────────────────────────────────────────────────

class VkSwapchain {
    fn create(device: VkDevice, surface: int, width: int, height: int) -> VkSwapchain
    fn destroy()
    fn acquire_next_image(semaphore: int) -> int
    fn present(queue: int, image_index: int) -> int
}

// ── Core API functions ────────────────────────────────────────────────────────

// Create a Vulkan instance from a filled-in VkInstanceCreateInfo struct.
fn vkCreateInstance(pCreateInfo: VkInstanceCreateInfo, pAllocator: ptr, pInstance: ptr) -> int32

// Create a logical device from a physical device and a filled-in VkDeviceCreateInfo.
fn vkCreateDevice(physicalDevice: VkPhysicalDevice, pCreateInfo: VkDeviceCreateInfo, pAllocator: ptr, pDevice: ptr) -> int32

// ── Utilities ─────────────────────────────────────────────────────────────────

// Returns a human-readable string for a Vulkan result code.
fn vk_result_string(result: int) -> str

// Returns 1 if a validation layer with the given name is available.
fn vk_layer_available(layer_name: str) -> int
```

### `vendors/vulkan/vulkan.hy`

The optional `.hy` companion adds a convenience wrapper that centralises error checking:

```hylian
include {
    std.errors,
    vendors.vulkan,
}

// Check a Vulkan result code and return an Error? if it is not VK_SUCCESS (0).
Error? vk_check(int result) {
    if (result != 0) {
        str msg = vk_result_string(result);
        return Err(msg);
    }
    return nil;
}
```

### Usage

Structs are declared without `new` — they are stack-allocated and zero-initialized. Fields are accessed with the same `.field` syntax as class fields. Pass a struct to a native function by pointer using `&`.

```hylian
include {
    std.io,
    vendors.vulkan,
}

Error? main() {
    VkApplicationInfo appInfo;
    appInfo.sType = 0;          // VK_STRUCTURE_TYPE_APPLICATION_INFO
    appInfo.pApplicationName = "MyApp";
    appInfo.applicationVersion = 1;
    appInfo.apiVersion = 4202496;  // VK_API_VERSION_1_0

    VkInstanceCreateInfo createInfo;
    createInfo.sType = 1;       // VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO
    createInfo.pNext = nil;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledLayerCount = 0;
    createInfo.enabledExtensionCount = 0;

    rawptr instance;
    int result = vkCreateInstance(&createInfo, nil, &instance);
    if (result != 0) {
        return Err("vkCreateInstance failed");
    }

    println("Vulkan instance created.");
    return nil;
}
```

---

## Example: Wrapping OpenGL (via GLFW + glad)

This example wraps GLFW for windowing and exposes a small subset of OpenGL via glad, a GL loader. Two packages are used — `glfw` and `gl` — each with its own `.hyi`.

### `vendors/glfw/glfw.hyi`

```
// Hylian vendor interface — vendors.glfw
// Wraps libglfw.so.3 (GLFW 3.x)
module vendors.glfw
link "libglfw.so.3"

// ── Window ────────────────────────────────────────────────────────────────────

class GLFWwindow {
    // Create a window. Pass 0 for monitor and share for a plain windowed window.
    fn create(width: int, height: int, title: str) -> GLFWwindow
    fn destroy()
    fn should_close() -> int
    fn swap_buffers()
    fn make_context_current()
    fn get_framebuffer_size(width: int, height: int)
}

// ── Library lifecycle ─────────────────────────────────────────────────────────

// Initialise the GLFW library. Returns 1 on success, 0 on failure.
fn glfw_init() -> int

// Terminate GLFW and free all resources.
fn glfw_terminate()

// Poll for pending events (non-blocking).
fn glfw_poll_events()

// Set the minimum OpenGL context version for the next window created.
fn glfw_window_hint_version(major: int, minor: int)

// Require a core profile context.
fn glfw_window_hint_core_profile()

// Return the time in seconds since GLFW was initialised.
fn glfw_get_time() -> float
```

### `vendors/gl/gl.hyi`

```
// Hylian vendor interface — vendors.gl
// Wraps OpenGL via glad (libGL.so.1 + glad loader)
module vendors.gl
link "libGL.so.1"

// Load all OpenGL function pointers via glad.
// Must be called after a GL context is current (i.e. after GLFWwindow.make_context_current).
// Returns 1 on success, 0 on failure.
fn gl_load() -> int

// ── State ─────────────────────────────────────────────────────────────────────

fn gl_clear_color(r: float, g: float, b: float, a: float)
fn gl_clear(mask: int)
fn gl_viewport(x: int, y: int, width: int, height: int)
fn gl_enable(cap: int)
fn gl_disable(cap: int)
fn gl_depth_func(func: int)

// ── Buffers ───────────────────────────────────────────────────────────────────

fn gl_gen_vertex_arrays(n: int, arrays: str)
fn gl_bind_vertex_array(vao: int)
fn gl_gen_buffers(n: int, buffers: str)
fn gl_bind_buffer(target: int, buffer: int)
fn gl_buffer_data(target: int, size: int, data: str, usage: int)

// ── Shaders ───────────────────────────────────────────────────────────────────

fn gl_create_shader(shader_type: int) -> int
fn gl_shader_source(shader: int, source: str)
fn gl_compile_shader(shader: int)
fn gl_get_shader_compile_status(shader: int) -> int
fn gl_get_shader_info_log(shader: int, out_buf: str, buf_size: int) -> int
fn gl_delete_shader(shader: int)

fn gl_create_program() -> int
fn gl_attach_shader(program: int, shader: int)
fn gl_link_program(program: int)
fn gl_get_program_link_status(program: int) -> int
fn gl_use_program(program: int)
fn gl_get_uniform_location(program: int, name: str) -> int
fn gl_uniform_1i(location: int, value: int)
fn gl_uniform_1f(location: int, value: float)
fn gl_uniform_mat4fv(location: int, count: int, transpose: int, value: str)

// ── Drawing ───────────────────────────────────────────────────────────────────

fn gl_draw_arrays(mode: int, first: int, count: int)
fn gl_draw_elements(mode: int, count: int, type: int, indices: str)

// ── Textures ──────────────────────────────────────────────────────────────────

fn gl_gen_textures(n: int, textures: str)
fn gl_bind_texture(target: int, texture: int)
fn gl_tex_image_2d(target: int, level: int, internal_format: int, width: int, height: int, border: int, format: int, type: int, data: str)
fn gl_generate_mipmap(target: int)
fn gl_tex_parameter_i(target: int, pname: int, param: int)

// ── Constants ────────────────────────────────────────────────────────────────

fn gl_COLOR_BUFFER_BIT() -> int
fn gl_DEPTH_BUFFER_BIT() -> int
fn gl_DEPTH_TEST() -> int
fn gl_TRIANGLES() -> int
fn gl_FLOAT() -> int
fn gl_UNSIGNED_INT() -> int
fn gl_ARRAY_BUFFER() -> int
fn gl_ELEMENT_ARRAY_BUFFER() -> int
fn gl_STATIC_DRAW() -> int
fn gl_VERTEX_SHADER() -> int
fn gl_FRAGMENT_SHADER() -> int
fn gl_TEXTURE_2D() -> int
fn gl_RGBA() -> int
```

### `linkle.hy`

```
project {
    name: "gl-demo",
    version: "0.1.0",
    author: "",
}

build {
    src: "src",
    main: "main",
    out: "build",
    bin: "gl-demo",
}

vendors {
    glfw: "vendors/glfw",
    gl:   "vendors/gl",
}

target run() {
    exec("./build/bin/gl-demo");
}

target clean() {
    exec("rm -rf build");
}
```

### Usage

```hylian
include {
    std.io,
    vendors.glfw,
    vendors.gl,
}

Error? main() {
    int ok = glfw_init();
    if (ok == 0) {
        return Err("GLFW init failed");
    }

    glfw_window_hint_version(3, 3);
    glfw_window_hint_core_profile();

    GLFWwindow window = GLFWwindow.create(800, 600, "Hylian + OpenGL");
    if (!window) {
        glfw_terminate();
        return Err("window creation failed");
    }

    window.make_context_current();

    int loaded = gl_load();
    if (loaded == 0) {
        window.destroy();
        glfw_terminate();
        return Err("failed to load OpenGL");
    }

    gl_viewport(0, 0, 800, 600);

    while (window.should_close() == 0) {
        gl_clear_color(0.1, 0.1, 0.15, 1.0);
        gl_clear(gl_COLOR_BUFFER_BIT());

        // draw calls go here

        window.swap_buffers();
        glfw_poll_events();
    }

    window.destroy();
    glfw_terminate();
    return nil;
}
```

---

## Pure-Hylian Vendor Packages

A vendor package does not have to wrap a native library. A package with only a `.hy` file (no `.hyi`, no `link` directive) is a pure-Hylian package. It is compiled, cached, and linked like any other vendor, but requires no native `.so` on the system.

This is useful for distributing utility code that you want to share across projects without folding it into the standard library or the main source tree.

```
vendors/
└── mathutils/
    └── mathutils.hy    ← pure Hylian, no .hyi needed
```

---

## Reference: `.hyi` Syntax Summary

| Directive | Syntax | Required |
|---|---|---|
| Module declaration | `module vendors.<name>` | Yes |
| Link directive | `link "libfoo.so.N"` | Only if wrapping a native library |
| Free function | `fn name(param: type, ...) -> type` | — |
| Free function (void) | `fn name(param: type, ...)` | — |
| Class declaration | `class Name { ... }` | — |
| Class method | `fn name(param: type, ...) -> type` | — |
| Struct declaration | `struct Name { fieldType fieldName ... }` | — |

Types in `.hyi` files use the same names as in Hylian source: `int`, `str`, `bool`, `float`, `void`, and class or struct names defined in the same file. Struct field types additionally support the sized integer and float aliases (`int8`, `uint8`, `int16`, `uint16`, `int32`, `uint32`, `float32`, `int64`, `uint64`) and the pointer aliases (`ptr`, `rawptr`) to match C struct layouts exactly.

A class method that returns the enclosing class type acts as a constructor (the `create` / `init` pattern). A method with no return type or a `void` return type that takes no arguments acts as a destructor (the `destroy` / `free` pattern). Neither is enforced — the names and signatures are fully up to you.

Structs have no constructor and no methods. In Hylian source, declare a struct variable without `new` (`StructType x;`) and pass it to native functions by pointer with `&x`.

---

## Tips for Native-Feeling C FFI

The following patterns make vendor library usage feel as natural as writing C.

### Struct Literals Instead of Manual Packing

Before struct literals were added, passing structs to C functions required manually packing fields into `uint64` words:

```hylian
// Old, error-prone way
uint64 rx = rect_x;
uint64 ry = rect_y;
rect_word0 = rx + ry * 4294967296;
uint64 rw = RECT_W;
uint64 rh = RECT_H;
rect_word1 = rw + rh * 4294967296;
SDL_RenderFillRect(renderer, &rect_word0);
```

With struct literals this becomes:

```hylian
// New, clean way
SDL_Rect r = SDL_Rect { x: rect_x, y: rect_y, w: RECT_W, h: RECT_H };
SDL_RenderFillRect(renderer, &r);
```

### Digit Separators for SDL / Hardware Constants

Use `_` inside literals to make masks and addresses more readable:

```hylian
const uint32 WIN_POS  = 0x2FFF_0000;   // SDL_WINDOWPOS_CENTERED_MASK
const uint64 PAGE     = 0x1_0000;       // 65536
const uint32 MAGIC    = 0xDEAD_BEEF;
```

### The `as` Cast for Event Field Extraction

SDL2 events pack multiple fields into a single 64-bit word. The `as` cast reads cleanly:

```hylian
uint64 w0    = ev_word0;
int ev_type  = w0 as uint32;    // low 32 bits = event type

uint64 w2    = ev_word2;
int sym      = w2 as uint32;    // low 32 bits = key symbol
```

### `null` for Nullable SDL Handles

SDL functions that fail return a null pointer. Use `null` (or `nil`) to check:

```hylian
SDL_Window win = SDL_CreateWindow("title", 0, 0, 800, 600, 4);
if (win == null) {
    println(SDL_GetError());
    SDL_Quit();
    return;
}
```

---

## Changelog — Vendor & FFI Improvements

### 🔴 Bug Fixes

**Static array `+16` header offset** (`lower.c`, `codegen_asm.c`)

`IR_ARRAY_LOAD`/`IR_ARRAY_STORE` were unconditionally adding `+16` to skip a heap-array header that doesn't exist for flat `.data` static arrays. This caused crashes and memory corruption when indexing any `static T name[N]` array used in FFI code. Fixed by detecting flat vs heap layout in `lower.c` and branching on `ins->extra_int` in `codegen_asm.c`.

**64-bit constant divide-by-zero** (`lexer.l`)

`atoi()` silently truncated values like `4294967296` (common SDL mask) to `0`, triggering `idiv 0` faults. The lexer now uses `strtoull` for both decimal and hex literals.

**Function-local static labels** (`lower.c`)

Statics declared inside a function body weren't emitting `.data` labels, causing NASM "undefined symbol" errors. Fixed with unique mangled names (`__static_N__varname`) and `LowerState` alias tracking.

### 🟡 New Features

| Feature | Syntax | Details |
|---|---|---|
| Stack struct literals | `SDL_Rect r = SDL_Rect { x: 10, y: 20 };` | Stack allocation via `IR_ALLOCA` + field init |
| `as` cast syntax | `word as uint32` | Infix alias for `cast<T>(expr)` |
| `null` literal | `if (win == null) { ... }` | Alias for `nil`, zero pointer constant |
| Digit separators | `0xFF_FF_FF_FF`, `4_294_967_296` | `_` stripped before parsing |

### 🟢 LSP Grammar Updates

| Change | File |
|---|---|
| `struct_literal_expr` + `field_init_list` nodes | `grammar.js`, `highlights.scm` |
| `as_cast_expr` node | `grammar.js`, `highlights.scm` |
| `static_var_stmt` in `statement` | `grammar.js` |
| `nil_literal` matches `"nil"` and `"null"` | `grammar.js` |
| `integer_literal` allows `_` and long hex | `grammar.js` |
| `as` keyword highlighted | `highlights.scm` (both grammar and Zed) |