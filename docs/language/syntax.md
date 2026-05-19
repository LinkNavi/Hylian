# Hylian Language Syntax Reference

This document is the comprehensive reference for Hylian syntax and language features. For error handling patterns see [error-handling.md](error-handling.md), and for the module system see [modules.md](modules.md).

> **Coming from Go?** Hylian will feel familiar: C-like syntax, explicit types, no garbage collector, and first-class error values. The main differences are that Hylian compiles straight to x86-64 assembly (no runtime), uses `class` instead of `struct`/`interface`, and gives you raw pointer access and inline assembly when you need to go low-level. Jump in at [Variables](#variables) and work your way down.

---

## Types

### Primitive Types

| Type | Size | Description |
|---|---|---|
| `int` | 64-bit | Signed integer |
| `str` | pointer | String (pointer to null-terminated character data) |
| `bool` | 8-bit | Boolean value: `true` or `false` |
| `float` | 64-bit | Double-precision floating point number |
| `void` | — | No value. Used as a return type when a function returns nothing, or with `nil` |
| `usize` | 64-bit | Unsigned pointer-width integer. Use for addresses, sizes, and offsets |
| `isize` | 64-bit | Signed pointer-width integer. Use for pointer arithmetic and signed offsets |

### Nullable Types

Any type can be made nullable by appending `?`. A nullable type can hold the value `nil` in addition to its normal range of values.

```hylian
int? count = nil;
str? name = "Alice";
Error? err = nil;
```

The most common nullable type in Hylian is `Error?`, used as a return type for functions that may fail. `nil` means success; a non-nil value means an error occurred.

### Class Types

User-defined classes are types. An instance is created with `new`:

```hylian
Player p = new Player("Bob", 100);
```

### `array<T>`

A heap-allocated, dynamically resizable array of elements of type `T`. Arrays have two built-in fields:

- `.len` — the number of elements currently stored
- `.cap` — the total allocated capacity

```hylian
array<int> nums = [1, 2, 3];
int length = nums.len;
int capacity = nums.cap;
```

An empty array declaration pre-allocates 8 slots:

```hylian
array<str> names;
```

Elements can be appended with `.push()` and removed from the end with `.pop()`:

```hylian
array<int> stack;
stack.push(10);
stack.push(20);
stack.push(30);
int last = stack.pop();    // 30; stack.len is now 2
```

> **Coming from Go?** This is roughly `append(slice, val)` and a manual pop. Unlike Go slices, `.push()` mutates the array in place.

### `multi<A | B | ...>`

A tagged union that can hold a value of any one of the listed types at a time. Two built-in fields are available:

- `.tag` — a 0-based integer index indicating which type is currently stored
- `.value` — the stored value

```hylian
multi<int | str> x = 42;
int tag = x.tag;   // 0 = int, 1 = str
int val = x.value;

multi<int | str> y = "hello";
// y.tag == 1
```

`multi<any>` is an untyped union that can hold a value of any type without specifying the possible variants up front.

---

## Variables

Variables are declared with their type first, followed by the name and an optional initializer. All statements are terminated with a semicolon.

```hylian
int x = 42;
str name = "Alice";
bool flag = true;
float pi = 3.14;
```

A variable can be declared without an initializer, in which case it holds a zero or nil value depending on its type:

```hylian
int count;
array<str> names;
```

> **Coming from Go?** Hylian has no `:=` short-variable declaration. Always write the type explicitly — `int x = 42;` not `x := 42`.

---

## String Interpolation

Hylian supports inline variable interpolation inside string literals using double curly braces `{{varname}}`. The variable is converted to its string representation at runtime.

```hylian
int score = 99;
println("Your score is {{score}}.");

str player = "Alice";
int health = 75;
println("{{player}} has {{health}} HP remaining.");
```

---

## Functions

### Declaration

The return type is written first, followed by the function name and its parameter list. The function body is enclosed in braces.

```hylian
int add(int a, int b) {
    return a + b;
}

str greet(str name) {
    return "Hello, {{name}}!";
}

void logMessage(str msg) {
    println(msg);
}
```

### Error-Returning Functions

Functions that may fail return `Error?`. Return `nil` to signal success, or `Err("message")` to signal failure.

```hylian
Error? save(str path) {
    // ... write to disk ...
    return nil;
}

Error? validateAge(int age) {
    if (age < 0) {
        return Err("age cannot be negative");
    }
    return nil;
}
```

### Calling Functions

```hylian
int result = add(3, 4);
str msg = greet("Bob");
Error? err = save("/tmp/data.txt");
```

---

## Classes

Classes are declared with the `public class` keywords. Fields are declared inside the class body with an access modifier (`private` or `public`). The constructor has the same name as the class and no return type.

```hylian
public class Player {
    private str name;
    private int health;

    Player(str n, int h) {
        name = n;
        health = h;
    }

    str getName() {
        return name;
    }

    int getHealth() {
        return health;
    }

    Error? setHealth(int h) {
        if (h < 0) {
            return Err("health cannot be negative");
        }
        health = h;
        return nil;
    }
}
```

### Instantiation

Use `new` to create an instance:

```hylian
Player p = new Player("Bob", 100);
```

### Calling Methods

```hylian
str name = p.getName();
int hp = p.getHealth();

Error? err = p.setHealth(80);
if (err) {
    panic(err.message());
}
```

---

## Error Handling

See [error-handling.md](error-handling.md) for the full reference. A brief summary:

```hylian
Error? doThing() {
    return Err("it broke");
}

Error? err = doThing();
if (err) {
    panic(err.message());
}
```

- `Err("message")` — constructs an error value
- `nil` — represents no error (success)
- `err.message()` — returns the error message as a `str`
- `err.code()` — returns an `int` error code (currently always `0`)
- `panic(msg)` — prints `msg` to stderr and exits with code 1; never returns

---

## Control Flow

### If / Else

```hylian
if (x > 0) {
    println("positive");
} else {
    println("non-positive");
}
```

Chained conditions:

```hylian
if (x > 0) {
    println("positive");
} else if (x < 0) {
    println("negative");
} else {
    println("zero");
}
```

### While Loop

```hylian
int i = 0;
while (i < 10) {
    println(i);
    i = i + 1;
}
```

### C-Style For Loop

```hylian
for (int i = 0; i < 5; i = i + 1) {
    println(i);
}
```

The increment expression in a C-style `for` loop can use `++` and `--` as a shorthand (see [Increment and Decrement](#increment-and-decrement)):

```hylian
for (int i = 0; i < 5; i++) {
    println(i);
}
```

### For-In Loop

Iterates over every element of an `array<T>`, binding a copy of each element to the loop variable:

```hylian
array<int> nums = [1, 2, 3, 4, 5];
for (n in nums) {
    println(n);
}
```

#### For-In by Reference

Prefix the loop variable with `&` to bind a reference to each element instead of a copy. Mutations through the reference modify the original array:

```hylian
array<int> nums = [1, 2, 3];
for (&n in nums) {
    *n = *n * 2;    // double every element in place
}
// nums is now [2, 4, 6]
```

> **Coming from Go?** This is similar to `for i := range slice` where you use `slice[i]` to mutate — except here the reference is handed to you directly.

### Break and Continue

`break` exits the nearest enclosing loop immediately. `continue` skips the rest of the current iteration and proceeds to the next. Both are supported in `while`, C-style `for`, and `for-in` loops.

```hylian
for (int i = 0; i < 10; i++) {
    if (i == 3) {
        continue;
    }
    if (i == 7) {
        break;
    }
    println(i);
}
```

---

## Switch Statement

A `switch` statement compares an expression against a series of `case` values and executes the matching branch. If no `case` matches, the optional `default` branch is taken.

```hylian
int code = 2;

switch (code) {
    case 1: {
        println("one");
    }
    case 2: {
        println("two");
    }
    case 3: {
        println("three");
    }
    default: {
        println("unknown");
    }
}
```

Each arm is wrapped in its own `{ }` block. Unlike C, there is no fall-through between cases.

`switch` works with any integer type, `str`, or an enum variant:

```hylian
switch (direction) {
    case Direction.North: { move_up(); }
    case Direction.South: { move_down(); }
    default:              { println("sideways"); }
}
```

> **Coming from Go?** This is Go's `switch` with explicit `case` values — no `break` needed because there is no fall-through. The `default` arm plays the role of Go's `default`.

---

## Arrays

### Literal Initialization

```hylian
array<int> nums = [10, 20, 30];
array<str> words = ["hello", "world"];
```

### Empty Array

Declares an array with no elements and 8 slots pre-allocated:

```hylian
array<str> names;
```

### Indexing

Indices are 0-based:

```hylian
int first = nums[0];
nums[1] = 99;
```

### Length and Capacity

```hylian
int len = nums.len;
int cap = nums.cap;
```

### Push and Pop

Append a value to the end of an array with `.push()`, and remove the last value with `.pop()`:

```hylian
array<int> nums = [10, 20, 30];
nums.push(40);         // [10, 20, 30, 40]
int last = nums.pop(); // last = 40; nums is [10, 20, 30] again
```

### Iterating

```hylian
array<int> nums = [10, 20, 30];
for (n in nums) {
    println(n);
}
```

---

## Multi (Tagged Union)

`multi<A | B | ...>` holds exactly one value whose type is one of the listed variants. The `.tag` field is a 0-based integer indicating the active variant, and `.value` holds the current value.

```hylian
multi<int | str> x = 42;
int tag = x.tag;    // 0 = int, 1 = str
int val = x.value;

multi<int | str> y = "hello";
// y.tag == 1
// y.value == "hello"
```

Use `x.tag` to branch on which variant is active:

```hylian
if (x.tag == 0) {
    println("it's an int");
} else {
    println("it's a str");
}
```

`multi<any>` is an untyped variant that places no constraints on what type the value may be:

```hylian
multi<any> val = 123;
```

---

## Enums

An `enum` defines a named set of integer constants. Each variant is accessed with the `EnumName.Variant` syntax and evaluates to its integer value.

```hylian
enum Direction {
    North,
    South,
    East,
    West,
}

Direction heading = Direction.North;
```

By default the variants are numbered from `0`. You can assign explicit integer values to any or all variants:

```hylian
enum Permission {
    Read    = 1,
    Write   = 2,
    Execute = 4,
}

uint8 perms = Permission.Read | Permission.Write;   // 3
```

Explicit values are useful for bitmasks, protocol constants, and hardware register fields.

Mark an enum `public` to export it from a module:

```hylian
public enum Color {
    Red,
    Green,
    Blue,
}
```

> **Coming from Go?** This is the explicit `iota` / `const` block pattern from Go, but with dedicated syntax. `Direction.North` in Hylian is like `North` in a Go const block — there is no implicit iota, but you get clean dot-access on the type name.

---

## Increment and Decrement

Hylian has prefix and postfix `++` and `--` operators for integer variables.

```hylian
int i = 0;
i++;      // postfix — i is now 1
++i;      // prefix  — i is now 2
i--;      // postfix — i is now 1
--i;      // prefix  — i is now 0
```

The prefix form returns the value **after** the change; the postfix form returns the value **before** the change.

```hylian
int a = 5;
int b = a++;    // b = 5,  a = 6
int c = ++a;    // c = 7,  a = 7
```

These are most commonly used as the update expression in a `for` loop:

```hylian
for (int i = 0; i < 10; i++) {
    println(i);
}
```

---

## Inline Assembly

The `asm { ... }` block allows embedding raw x86-64 NASM assembly directly in a Hylian function. This is an advanced feature intended for low-level programming where direct hardware or register access is required.

```hylian
void cpuid() {
    asm {
        mov eax, 0
        cpuid
    }
}
```

The assembly is emitted verbatim into the generated `.asm` output. No register saving or calling-convention handling is performed automatically — the programmer is responsible for preserving any registers that Hylian's code generator may depend on.

---

## Floating-Point Numbers

The `float` type (alias `float64`) is a 64-bit IEEE 754 double-precision number. `float32` is the 32-bit single-precision variant. Float arithmetic uses SSE2 instructions.

```hylian
float pi    = 3.14159;
float e     = 2.71828;
float32 x   = 1.5;
float64 big = 1.23456789e10;
```

All standard arithmetic operators work on floats:

```hylian
float a = 10.0;
float b = 3.0;
float sum  = a + b;   // 13.0
float diff = a - b;   // 7.0
float prod = a * b;   // 30.0
float quot = a / b;   // 3.333...
```

Floats can be printed with `println` or `print`:

```hylian
include { std.io, }

Error? main() {
    float result = 9.81 * 2.0;
    println(result);    // prints the decimal representation
    return nil;
}
```

> **Note:** Integer and float types are not implicitly converted. Use `cast<float>(n)` to convert an integer to a float, or `cast<int>(f)` to truncate a float to an integer.

---

## Fixed-Size Integer Types

In addition to the general-purpose `int` (64-bit) type, Hylian provides a full set of fixed-width integer types for low-level and hardware programming where exact sizes are required.

| Type | Size | Signedness | Range |
|---|---|---|---|
| `int8` | 8-bit | Signed | −128 to 127 |
| `int16` | 16-bit | Signed | −32,768 to 32,767 |
| `int32` | 32-bit | Signed | −2,147,483,648 to 2,147,483,647 |
| `int64` | 64-bit | Signed | −9,223,372,036,854,775,808 to 9,223,372,036,854,775,807 |
| `uint8` | 8-bit | Unsigned | 0 to 255 |
| `uint16` | 16-bit | Unsigned | 0 to 65,535 |
| `uint32` | 32-bit | Unsigned | 0 to 4,294,967,295 |
| `uint64` | 64-bit | Unsigned | 0 to 18,446,744,073,709,551,615 |
| `usize` | 64-bit | Unsigned | 0 to 18,446,744,073,709,551,615 |
| `isize` | 64-bit | Signed | −9,223,372,036,854,775,808 to 9,223,372,036,854,775,807 |

`usize` and `isize` are semantically distinct from `uint64`/`int64` — they communicate intent: pointer-width values, memory addresses, sizes, and offsets.

```hylian
uint8 flags = 0xFF;
uint16 port = 0x3F8;
uint32 checksum = 0xDEADBEEF;
uint64 timestamp = 1700000000000;

usize addr = 0xB8000;    // VGA framebuffer address
isize offset = -16;      // signed byte offset
```

Fixed-size types are particularly important when defining hardware structures, manipulating memory-mapped I/O registers, or writing data to disk or network in a defined binary format.

---

## Volatile Pointer Access

The `volatile` keyword forces every read or write through a pointer to be emitted as a real memory access, bypassing any compiler caching, reordering, or elimination. This is essential when accessing memory-mapped hardware registers, where the act of reading or writing has a side effect independent of the value.

### Reading

```hylian
usize vga = 0xB8000;
uint8 val = volatile *vga;
```

### Writing

```hylian
usize vga = 0xB8000;
volatile *vga = 0x4F41;    // write 'A' with red-on-black attribute
```

### Why it matters

Without `volatile`, the compiler may legally eliminate a write it considers redundant, or cache a read it assumes cannot change between accesses. Hardware registers can change at any time (e.g. an interrupt status register) or require that every write reaches the bus (e.g. a VGA cell). `volatile` guarantees neither is optimized away.

```hylian
// Poll a hardware status register until the ready bit is set.
// Without volatile, the compiler could hoist the read out of the loop.
usize status_reg = 0xFEE00300;
while ((volatile *status_reg & 0x1000) != 0) {
    // wait
}
```

---

## Pointers and References

Hylian has two pointer-like types:

| Type | Syntax | Description |
|---|---|---|
| Reference | `&T` | A safe, non-null reference to a value of type `T` |
| Raw pointer | `*T` | An untyped pointer — can be null, requires `unsafe` to dereference |

### Address-of (`&expr`)

The `&` prefix operator takes the address of a variable and produces a reference:

```hylian
int x = 42;
&int ref = &x;      // ref points to x
```

### Dereference (`*expr`)

The `*` prefix operator loads the value at the address a pointer or reference holds:

```hylian
int x   = 42;
&int ref = &x;
int val  = *ref;    // val = 42
```

Dereferencing a reference is safe. Dereferencing a raw `*T` pointer requires an `unsafe` block (see below).

### Raw Pointer Types

Raw pointer types are declared with a `*` before the type name. They are most often produced with `cast<*T>`:

```hylian
usize addr   = 0xB8000;
*uint16 cell = cast<*uint16>(addr);
```

### References as Function Parameters

Passing a reference lets a function modify the caller's variable without returning a new value:

```hylian
void increment(&int n) {
    *n = *n + 1;
}

int counter = 0;
increment(&counter);    // counter is now 1
```

> **Coming from Go?** `&T` in Hylian is close to `*T` in Go — both are "a pointer to a T that you're expected to dereference safely". Hylian's `*T` (raw pointer) is closer to `unsafe.Pointer` — it bypasses the type system and requires an `unsafe` block.

---

## `unsafe` Blocks

Raw pointer dereferences (outside of `volatile` access) must appear inside an `unsafe` block. This makes it easy to audit the parts of your code that interact directly with arbitrary memory.

```hylian
usize addr    = 0xDEAD0000;
*uint32 ptr   = cast<*uint32>(addr);

unsafe {
    uint32 val = *ptr;     // dereference a raw pointer — allowed inside unsafe
    *ptr = 0xCAFEBABE;     // write through a raw pointer
}
```

Only the dereference itself needs to be inside the `unsafe` block. Obtaining an address, casting types, and reading through a `volatile` pointer are all safe operations.

> **Coming from Go?** Go has `unsafe.Pointer` and the `unsafe` package. Hylian's `unsafe { }` block is the direct equivalent — a scoped annotation that says "I know what I am doing, the compiler should trust me here".

---

## `const` Globals

A top-level `const` declaration defines a compile-time constant that is substituted wherever it is used. Constants must have an initializer.

```hylian
const int MAX_PLAYERS  = 8;
const str APP_VERSION  = "1.0.0";
const float GRAVITY    = 9.81;
```

Mark a constant `public` to export it from a module:

```hylian
public const int SCREEN_WIDTH  = 1920;
public const int SCREEN_HEIGHT = 1080;
```

Constants are zero-cost — no memory is allocated; every use is replaced with the literal value at compile time.

> **Coming from Go?** This is `const` in Go, but with an explicit type annotation.

---

## `packed` Classes

By default, the compiler may insert padding bytes between class fields to satisfy alignment requirements. A `packed` class disables all padding — fields are laid out contiguously in memory exactly as declared.

This is required when a class must match a hardware-defined binary layout, such as a GDT descriptor, IDT entry, or a network packet header.

```hylian
packed class GdtEntry {
    public uint16 limit_low;
    public uint16 base_low;
    public uint8  base_mid;
    public uint8  access;
    public uint8  granularity;
    public uint8  base_high;

    GdtEntry(uint32 base, uint32 limit, uint8 access, uint8 gran) {
        limit_low   = limit & 0xFFFF;
        base_low    = base & 0xFFFF;
        base_mid    = (base >> 16) & 0xFF;
        this.access = access;
        granularity = ((limit >> 16) & 0x0F) | (gran & 0xF0);
        base_high   = (base >> 24) & 0xFF;
    }
}
```

Without `packed`, the compiler might silently insert padding between `base_mid` and `access`, corrupting the structure when it is loaded into the CPU's GDTR register.

Another common use is an IDT gate descriptor:

```hylian
packed class IdtEntry {
    public uint16 offset_low;
    public uint16 selector;
    public uint8  ist;
    public uint8  type_attr;
    public uint16 offset_mid;
    public uint32 offset_high;
    public uint32 reserved;

    IdtEntry(usize handler, uint16 sel, uint8 attr) {
        offset_low  = handler & 0xFFFF;
        selector    = sel;
        ist         = 0;
        type_attr   = attr;
        offset_mid  = (handler >> 16) & 0xFFFF;
        offset_high = (handler >> 32) & 0xFFFFFFFF;
        reserved    = 0;
    }
}
```

> **Note:** Because `packed` removes padding, unaligned field accesses may be slower on some architectures. Only use `packed` where the layout must match an external binary contract.

---

## `union` Classes

A `union class` is a C-style union — all fields share the same memory offset (offset 0), and the total size of the union equals the size of its largest field. Only one field holds a meaningful value at any given time; writing to one field overwrites the memory shared by all others.

Use `union class` when you need to reinterpret raw bytes as different types — hardware registers, protocol headers, type-punning, or working with external C ABIs that use `union`.

```hylian
union class IntOrFloat {
    public int   as_int;
    public uint32 as_uint;
}
```

All four fields above share offset 0. The size of the union is the size of the largest field.

### Declaring a Union

```hylian
union class Register {
    public uint64 qword;
    public uint32 dword;
    public uint16 word;
    public uint8  byte;
}
```

A `public` prefix on the class exposes it from a module:

```hylian
public union class IpAddress {
    public uint32 raw;
    public uint8  octets;
}
```

### Using a Union

Instantiate with `new` and access fields like any other class:

```hylian
Register reg = new Register();
reg.qword = 0xDEAD_BEEF_CAFE_1234;

uint32 lo = reg.dword;   // lower 32 bits of qword
uint16 w  = reg.word;    // lower 16 bits
uint8  b  = reg.byte;    // lowest byte
```

Or use a stack-allocated struct literal:

```hylian
Register reg = Register { qword: 0xFF00_FF00_FF00_FF00 };
uint8 low_byte = reg.byte;
```

### Type-Punning Example

A common use is reading the raw bit pattern of a float:

```hylian
union class FloatBits {
    public float  f;
    public uint32 bits;
}

FloatBits fb = new FloatBits();
fb.f = 1.0;
uint32 raw = fb.bits;   // IEEE 754 bit pattern of 1.0 = 0x3F800000
```

### Hardware Register Overlay

Union classes are well-suited for overlaying a hardware register that can be accessed as different widths:

```hylian
union class PciConfigWord {
    public uint32 full;
    public uint16 lo;
}

PciConfigWord cfg = new PciConfigWord();
cfg.full = 0x8086_1234;
uint16 vendor_id = cfg.lo;   // 0x1234
```

> **Note:** Unlike `packed class`, a `union class` cannot have a constructor (`ctor`) — initialise fields directly after construction or use a struct literal. Only field declarations are permitted in the body; methods are not supported.

> **Note:** The total allocated size is rounded up to the nearest 8-byte boundary. A union with a single `uint8` field will still occupy 8 bytes.

---

## `naked` Functions

A `naked` function emits no prologue or epilogue — the compiler will not generate any stack frame setup, register saves, or `ret` instructions around the function body. The programmer is fully responsible for the calling convention.

`naked` is required for:

- **Interrupt and exception handlers** — the CPU pushes its own state onto the stack in a specific format; a compiler-generated prologue would corrupt it.
- **CPU entry points** — such as the kernel entry called directly from a bootloader, where no valid stack frame exists yet.
- **Context-switch routines** — where full control over register saves and stack manipulation is needed.

```hylian
naked void divide_error_handler() {
    asm {
        pusha
        call divide_error_impl
        popa
        iretq
    }
}
```

Because no `ret` is generated, every `naked` function **must** end its own execution explicitly — usually with `iretq` for interrupt handlers, or a manual `ret` inside an `asm` block.

```hylian
naked void kernel_entry() {
    asm {
        ; Set up a stack before doing anything else
        mov rsp, 0x7C00
        call kernel_main
        hlt
    }
}
```

> **Warning:** Do not write Hylian statements that generate stack-relative accesses (such as local variable declarations) inside a `naked` function unless you have already established a valid stack pointer. Doing so is undefined behaviour.

---

## `static` Global Variables

A `static` variable is declared at the top level of a file (outside any function or class). It has program lifetime — it is allocated in the `.data` section and persists for the entire run of the program.

```hylian
static int ticks = 0;
static uint8 current_color = 0x07;
static usize cursor_pos = 0xB8000;
```

`static` variables are mutable and accessible from any function in the same file:

```hylian
static int ticks = 0;

void timer_tick() {
    ticks = ticks + 1;
}

int get_ticks() {
    return ticks;
}
```

Static variables are zero-initialized if no initializer is provided:

```hylian
static int counter;    // initialized to 0
```

Add `public` to export the variable from a module:

```hylian
public static int global_counter = 0;
```

### Static Fixed-Size Arrays

A `static` array with a compile-time size is declared with brackets. It lives in the `.bss` or `.data` section for the lifetime of the program:

```hylian
static int scores[64];          // 64 zero-initialized ints
static uint8 framebuffer[4096]; // 4 KiB byte buffer
```

Export a static array from a module with `public`:

```hylian
public static uint8 shared_buf[256];
```

Static fixed-size arrays are indexed and iterated exactly like heap arrays:

```hylian
static int table[4];

table[0] = 10;
table[1] = 20;
table[2] = 30;
table[3] = 40;

for (int i = 0; i < 4; i++) {
    println(table[i]);
}
```

> **Note:** `static` globals are not thread-safe. In a kernel or multi-core environment, accesses to shared statics must be protected by disabling interrupts or using appropriate synchronization.

---

## Tuples and Multi-Value Returns

A function can return more than one value by declaring a tuple return type. Tuple types are written as a parenthesised, comma-separated list of types:

```hylian
(int, int) divmod(int a, int b) {
    return a / b, a % b;
}
```

The caller receives a tuple value and can access its fields by index:

```hylian
(int, int) result = divmod(17, 5);
int quotient  = result.0;    // 3
int remainder = result.1;    // 2
```

Tuples can hold more than two elements:

```hylian
(str, int, bool) describe(int n) {
    if (n > 0) { return "positive", n, true; }
    return "non-positive", n, false;
}
```

Tuple literals can also appear as local values:

```hylian
(int, str) pair = (42, "hello");
int  num  = pair.0;
str  text = pair.1;
```

> **Coming from Go?** Multi-value returns in Go (`return a, b`) work the same way in Hylian. The main difference is that Hylian makes the tuple type explicit in the signature.

---

## Bitwise Operators

Hylian supports all six standard bitwise operators. They operate on integer types (`int`, `uint8`–`uint64`, `usize`, `isize`).

| Operator | Name | Description |
|---|---|---|
| `&` | AND | Sets each bit where both operands have a 1 |
| `\|` | OR | Sets each bit where either operand has a 1 |
| `^` | XOR | Sets each bit where exactly one operand has a 1 |
| `~` | NOT | Inverts every bit (unary prefix operator) |
| `<<` | Left shift | Shifts bits left, filling with zeros |
| `>>` | Right shift | Shifts bits right (logical for unsigned types) |

### Precedence

Bitwise operators sit **below** arithmetic operators (`+`, `-`, `*`, `/`) and **above** logical operators (`&&`, `||`) in the precedence hierarchy. Use parentheses liberally when mixing them to make intent explicit.

### Examples

```hylian
// Flag manipulation
uint8 flags = 0b00001010;
uint8 masked  = flags & 0x0F;       // keep low nibble
uint8 set     = flags | 0x01;       // set bit 0
uint8 toggled = flags ^ 0x02;       // toggle bit 1
uint8 inv     = ~flags;             // invert all bits

// Checking individual bits
int cr0 = 0x80000001;
int pe = cr0 & 1;                   // Protection Enable bit (bit 0)
int pg = (cr0 >> 31) & 1;           // Paging bit (bit 31)

// Setting a bit with a shift
cr0 = cr0 | (1 << 16);              // set Write Protect bit (bit 16)

// Page-table math: align an address down to a 4 KiB page boundary
usize addr = 0xB8ABC;
usize page = addr & ~0xFFF;         // 0xB8000

// Extract byte lanes from a 32-bit value
uint32 val  = 0xDEADBEEF;
uint8  lo   = cast<uint8>(val & 0xFF);          // 0xEF
uint8  hi   = cast<uint8>((val >> 24) & 0xFF);  // 0xDE
```

> **Note:** `~` is a **unary prefix** operator. Write `~x`, not `x~`.

---

## Compound Assignment Operators

Hylian supports the following compound assignment operators. Each reads the current value of the left-hand side, applies the operation with the right-hand side, and stores the result back.

| Operator | Equivalent to |
|---|---|
| `x += expr` | `x = x + expr` |
| `x -= expr` | `x = x - expr` |
| `x *= expr` | `x = x * expr` |
| `x /= expr` | `x = x / expr` |
| `x %= expr` | `x = x % expr` |

```hylian
int score = 100;
score += 10;    // 110
score -= 5;     // 105
score *= 2;     // 210
score /= 3;     // 70
score %= 8;     // 6
```

---

## Hex Literals and Digit Separators

Integer literals can be written in hexadecimal using the `0x` prefix. They are valid wherever an integer expression is expected and may be assigned to any integer type.

```hylian
usize vga      = 0xB8000;                   // VGA text buffer
usize lapic    = 0xFEE00000;                // Local APIC base (typical)
usize higher   = 0xFFFFFFFF80000000;        // Higher-half kernel base
uint32 magic   = 0xDEADBEEF;
uint16 port    = 0x3F8;                     // COM1 serial port
uint8  color   = 0x1F;                      // white-on-blue VGA attribute
```

Hex literals are particularly useful anywhere an address, bitmask, or hardware constant appears. There is no separate `0b` binary literal syntax — use hex to represent bitmasks, or spell them out with shifts and `|`.

### Digit Separators (`_`)

An underscore `_` can appear anywhere inside an integer or hex literal and is ignored by the compiler. This makes large numbers easier to read:

```hylian
uint32 mask   = 0xFF_FF_FF_FF;          // same as 0xFFFFFFFF
uint64 big    = 0x1_0000_0000;          // 4294967296
uint64 count  = 4_294_967_296;          // decimal with separators
uint64 net    = 0xC0_A8_00_01;          // 192.168.0.1 packed as u32
```

Underscores are stripped before parsing, so `4_294_967_296` and `4294967296` are identical at the bit level. Both decimal and hex literals support separators.

---

## `exit()`

`exit(code)` terminates the program immediately with the given integer exit code. It never returns.

```hylian
exit(0);    // success
exit(1);    // general error
```

This is distinct from `panic()`: `exit` is a clean, expected termination; `panic` is for unrecoverable programming errors and always prints a message to stderr before exiting with code `1`.

```hylian
if (args.len == 0) {
    println("Usage: program <input>");
    exit(1);
}
```

> **Coming from Go?** This is `os.Exit(code)` — but built-in, no import required.

---

## `cast<T>(expr)` and `expr as T`

`cast<T>` converts a value from one type to another at compile time (for constants) or with a truncating/reinterpreting conversion at runtime.

The `as` keyword is an alternative infix syntax that means exactly the same thing. Use whichever reads more naturally in context:

```hylian
int wide  = 300;
uint8 b1  = cast<uint8>(wide);   // prefix form
uint8 b2  = wide as uint8;       // infix form — same result
```

Both forms desugar to the same `cast` IR instruction, so there is no performance difference.

### Truncation to a smaller type

When casting to a narrower integer type the value is truncated to the low bits of the target width:

```hylian
int   wide  = 300;
uint8 byte  = cast<uint8>(wide);    // prefix:  300 & 0xFF = 44
uint8 byte2 = wide as uint8;        // infix:   same result
```

### Integer to pointer-sized integer

Use `cast<usize>` to treat an arbitrary integer as a raw address:

```hylian
int   raw  = 0xB8000;
usize addr = cast<usize>(raw);      // now safe to use as a pointer base
```

### Pointer cast

Use `cast<*T>` to reinterpret a `usize` address as a typed pointer suitable for dereference or volatile access:

```hylian
usize pci_base = 0xE0000000;
*uint32 reg    = cast<*uint32>(pci_base);
volatile *reg  = 0xDEADBEEF;
```

### Widening

Casting a narrow type to a wider one zero-extends unsigned types and sign-extends signed types:

```hylian
uint8  small = 0xFF;
uint32 wide  = cast<uint32>(small);   // 0x000000FF
int8   neg   = -1;
int32  sext  = cast<int32>(neg);      // 0xFFFFFFFF  (-1)
```

---

### `as` with event / SDL patterns

The `as` form is especially readable when extracting fields from packed integers:

```hylian
uint64 word0 = ev_word0;
int ev_type  = word0 as uint32;         // mask to low 32 bits

uint64 raw   = some_register;
uint8  byte  = raw as uint8;            // low byte only
```

---

## `null` Literal

`null` is an alias for `nil` — both represent a zero-valued pointer constant. Use whichever reads more naturally. They compile to exactly the same value.

```hylian
*void p = null;          // null pointer
if (p == nil) { ... }    // nil and null compare equal
```

When calling C functions that accept or return nullable pointers, either literal works:

```hylian
SDL_Window win = SDL_CreateWindow("title", 0, 0, 800, 600, 0);
if (win == null) {
    println("window creation failed");
    SDL_Quit();
}
```

---

## Stack-Allocated Struct Literals

A struct or vendor class instance can be allocated on the stack using brace-initializer syntax:

```hylian
TypeName var = TypeName { field: value, field: value, ... };
```

The compiler emits an `IR_ALLOCA` for the hidden stack slot, then an `IR_SET_FIELD` for each named field. The result is a stack pointer to the fully-initialized struct — exactly what C FFI functions expecting a `T*` parameter need.

```hylian
SDL_Rect r = SDL_Rect { x: 10, y: 20, w: 100, h: 80 };
SDL_RenderFillRect(renderer, &r);
```

Field names must match the struct/class definition. Fields not listed in the initializer are zero-initialized. Order of the fields in the brace list does not have to match the struct layout.

```hylian
// Partial initialization — missing fields default to 0
SDL_Rect clip = SDL_Rect { w: 640, h: 480 };   // x=0, y=0
```

> **Tip:** Use struct literals any time you need to pass a temporary struct to a C function. It avoids manually packing fields into `uint64` words and is fully type-safe.

---

## `addrof_fn`

`addrof_fn(functionName)` is a low-level builtin that returns the absolute address of a function as a `usize`. This is useful when you need to pass a function pointer to hardware (e.g. loading an IDT entry with a handler address) or to foreign code.

```hylian
usize handler_addr = addrof_fn(divide_error_handler);
```

The result is a raw integer address. Cast it to a function-pointer type or pass it directly to a `packed` class constructor for hardware tables.

---

## `size_of(T)`

`size_of(T)` returns the size of type `T` in bytes as a compile-time `usize` constant. It accepts any primitive type, fixed-width integer type, class, or `packed` class.

```hylian
usize a = size_of(uint8);    // 1
usize b = size_of(uint16);   // 2
usize c = size_of(uint32);   // 4
usize d = size_of(uint64);   // 8
usize e = size_of(int);      // 8  (int is always 64-bit)
usize f = size_of(usize);    // 8  (on a 64-bit target)
```

For `packed` classes the result is exactly the sum of the field sizes (no padding):

```hylian
packed class GdtEntry {
    public uint16 limit_low;    // 2
    public uint16 base_low;     // 2
    public uint8  base_mid;     // 1
    public uint8  access;       // 1
    public uint8  granularity;  // 1
    public uint8  base_high;    // 1
}

usize gdt_entry_size = size_of(GdtEntry);   // 8
```

`size_of` is commonly used to compute byte offsets in manually managed memory regions, to validate that a struct has the expected on-wire size, or to pass element sizes to `memset`/`memcpy`.

---

## Modules

Use `include` to import modules before using their declarations. See [modules.md](modules.md) for the full reference.

```hylian
include {
    std.io,
    std.strings,
}
```

Multiple modules can appear in a single `include` block, separated by commas. A trailing comma after the last entry is allowed.

---

## Operator Precedence Summary

From highest (tightest binding) to lowest:

| Level | Operators |
|---|---|
| 1 — Unary | `!`, `~`, `-` (negate), `&` (address-of), `*` (dereference), `++`, `--` (prefix) |
| 2 — Cast | `cast<T>(expr)`, `expr as T` |
| 3 — Multiplicative | `*`, `/`, `%` |
| 4 — Additive | `+`, `-` |
| 5 — Shift | `<<`, `>>` |
| 6 — Bitwise AND | `&` |
| 7 — Bitwise XOR | `^` |
| 8 — Bitwise OR | `\|` |
| 9 — Comparison | `==`, `!=`, `<`, `<=`, `>`, `>=` |
| 10 — Logical AND | `&&` |
| 11 — Logical OR | `\|\|` |
| 12 — Assignment | `=`, `+=`, `-=`, `*=`, `/=`, `%=` |

When in doubt, use parentheses to make intent explicit — especially when mixing bitwise and comparison operators.

---

## Changelog — Recent Compiler Additions

The entries below track breaking fixes and new language features added in recent sessions. They are listed from most impactful to least.

### 🔴 Bug Fixes

**Static array `+16` header offset** (`lower.c`, `codegen_asm.c`)

`IR_ARRAY_LOAD` and `IR_ARRAY_STORE` were unconditionally adding `+16` bytes to skip a heap-array `[len][cap]` header that doesn't exist for flat `.data` static arrays. This caused memory corruption and crashes when indexing any `static T foo[N]` array.

- `lower.c` — `NODE_INDEX` and `NODE_INDEX_ASSIGN` now detect whether the indexed object is a function-local or top-level static array. When it is, they emit `IR_ADDROF` (to get the raw label address, not its value) and set `extra_int = 1` plus `str_extra = elem_type` on the generated `IR_ARRAY_LOAD`/`IR_ARRAY_STORE` instruction.
- `LowerState` gained `top_static_array_names/types/count` fields to track module-level and top-level static arrays, plus a `lower_find_top_static_array_type()` helper.
- `codegen_asm.c` — both `IR_ARRAY_LOAD` and `IR_ARRAY_STORE` now branch on `ins->extra_int & 1`: flat layout uses `field_byte_width(str_extra)` as the element stride with no `+16`; heap layout preserves the original stride-8 + `+16` behavior.

**`IR_MOD`/`IR_DIV` divide-by-zero with 64-bit constants** (`lexer.l`)

`atoi()` silently truncated values like `4294967296` to `0`, causing `idiv 0` crashes at runtime. The lexer now uses `(long)(unsigned long long)strtoull(...)` for both decimal and hex literals, allowing the full 64-bit unsigned range.

**Function-local static labels undefined** (`lower.c`)

Static variables declared inside a function body weren't emitting `IR_STATIC_VAR` instructions, so their `.data` labels never appeared and NASM reported "undefined symbol". The fix emits `IR_STATIC_VAR` with unique mangled names (`__static_N__varname`) and registers name aliases in `LowerState` for transparent name rewriting.

### 🟡 New Features

**Stack-allocated struct literals** (`ast.h`, `parser.y`, `typecheck.c`, `lower.c`)

Added `ClassName { field: value, ... }` syntax — see [Stack-Allocated Struct Literals](#stack-allocated-struct-literals) above.

**`expr as Type` cast syntax** (`lexer.l`, `parser.y`)

Added `"as"` → `AS` token and a `| expr AS type` production. Desugars to the same `"cast"` binary op as `cast<T>(expr)` — see [`cast<T>(expr)` and `expr as T`](#castTexpr-and-expr-as-t) above.

**`null` literal** (`lexer.l`)

`"null"` now returns the `NIL` token — same as `nil`, a zero-valued pointer constant. See [`null` Literal](#null-literal) above.

**Underscore digit separators and large hex literals** (`lexer.l`)

Added `strip_underscores()` helper; hex rule updated to `0[xX][0-9a-fA-F_]+` and decimal to `[0-9][0-9_]*`, both parsed with `strtoull`. See [Digit Separators](#digit-separators-_) above.

### 🟢 LSP Grammar Updates

The Tree-sitter grammar (`lsp/grammar/grammar.js`) and query files (`.scm`) were updated to match the new features:

- `struct_literal_expr` and `field_init_list` nodes added
- `as_cast_expr` node added
- `static_var_stmt` added to the `statement` production (function-local statics)
- `nil_literal` extended to match both `"nil"` and `"null"`
- `integer_literal` regex updated to allow `_` digit separators and long hex
- Highlight rules updated for `as`, `null`, and `struct_literal_expr` in both grammar queries and Zed editor queries