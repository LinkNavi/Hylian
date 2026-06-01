# Termina Backend

The Termina backend is a bytecode compiler target for the Hylian programming language. It generates portable bytecode that runs on the Termina virtual machine, a simple 64-bit RISC-style VM designed specifically for Hylian.

## Overview

Unlike other Hylian backends that generate native assembly code (x86_64, ARM64), the Termina backend produces bytecode for a virtual machine. This provides several benefits:

- **Portability**: Bytecode runs on any platform with a Termina VM
- **Simplicity**: Easier to understand and debug than native assembly
- **Safety**: VM provides isolation and controlled execution
- **Learning**: Great for understanding compilation and VM design

## Getting Started

### Compiling to Termina

Use the `--target termina` flag with the Hylian compiler:

```bash
hylian program.hy -o program.bin --target termina
```

Or with linkle, set the target in `Linkle.toml`:

```toml
[build]
target = "termina"
```

Then build:

```bash
linkle build
```

### Running Termina Bytecode

The Termina VM is located in the `Termina/emu/` directory:

```bash
# Build the VM (one time)
cd Termina/emu
g++ -std=c++11 main.cpp -o termina-vm

# Run your program
./termina-vm program.bin

# Debug mode with execution trace
./termina-vm -trace program.bin
```

## Architecture

### Registers

Termina has 16 64-bit general-purpose registers:

- **r0-r12**: General purpose
- **r13 (SP)**: Stack pointer
- **r14 (LR)**: Link register (return address)
- **r15 (PC)**: Program counter

### Memory Layout

| Address Range | Purpose |
|---------------|---------|
| `0x0000 - 0x0FFF` | Reserved |
| `0x1000 - 0x1FFF` | Data section (strings, constants) |
| `0x2000 - 0xFFFF7` | Heap and stack space |
| `0xFFFF8 - 0xFFFFFF` | Stack (grows down) |
| `0x10000+` | Code section |

Total address space: 16 MB

### Binary Format

Termina bytecode files (`.bin`) have the following structure:

**Header (16 bytes):**
```
Offset  Size  Field       Description
------  ----  ----------  ------------------------------------
0       4     magic       0x4D524554 ("TERM" in little-endian)
4       4     data_size   Size of data section in bytes
8       4     data_off    Offset to data section (always 16)
12      4     code_count  Number of 32-bit instructions
```

**Data Section:**
- Starts at byte 16
- Contains string constants and read-only data
- Loaded into VM memory at address `0x1000`

**Code Section:**
- Starts at byte `16 + data_size`
- Contains 32-bit instruction words
- Loaded into VM memory at address `0x10000`

## Instruction Set

Termina has 31 instructions organized into categories:

### Arithmetic
- `ADD`, `SUB`, `MUL`, `DIV`, `MOD`, `ADDI`

### Bitwise
- `AND`, `OR`, `XOR`, `NOT`, `SHL`, `SHR`

### Memory
- `LOAD`, `STORE`

### Data Movement
- `MOV`, `MOVI`, `MOVHI`

### Control Flow
- `JMP`, `JMPI` (unconditional jumps)
- `JZ`, `JNZ`, `JL`, `JG` (conditional jumps)

### Stack
- `PUSH`, `POP`

### Function Calls
- `CALL`, `CALLI`, `RET`

### System
- `INT` (system calls)
- `HLT` (halt execution)
- `NOP` (no operation)

For complete instruction reference, see [Termina/docs/ISA.md](../../Termina/docs/ISA.md).

## Calling Convention

### Function Arguments
- First 6 arguments: `r0-r5`
- Additional arguments: on stack (if needed)

### Return Values
- Return value in `r0`
- 64-bit or smaller values only

### Stack Management
- Stack pointer (`SP`/`r13`) points to top of stack
- Stack grows downward (high to low addresses)
- Return address stored in link register (`LR`/`r14`) by `CALL`

### Register Usage
- **Caller-saved**: `r0-r12`
- **Callee-saved**: None (simplified convention)
- **Link register**: Saved automatically by `CALL`/`CALLI`

## System Calls

The `INT` instruction provides system calls:

### INT 0 - Write

Write data to a file descriptor.

**Input:**
- `r0` = file descriptor (1 = stdout, 2 = stderr)
- `r1` = buffer address
- `r2` = length in bytes

**Output:**
- `r0` = bytes written

**Example:**
```
MOVI r0, 1        ; stdout
MOVI r1, 0x1000   ; buffer address
MOVI r2, 13       ; length
INT  0            ; write syscall
```

## Language Features Support

### Supported
- ✅ Functions and function calls
- ✅ Local variables
- ✅ Arithmetic and bitwise operations
- ✅ Control flow (if, while, for)
- ✅ String literals
- ✅ Integer types
- ✅ Boolean types
- ✅ Basic I/O (println)
- ✅ Memory operations

### Limited Support
- ⚠️ Classes/OOP (stub implementation)
- ⚠️ Arrays (basic support)
- ⚠️ Dynamic allocation (bump allocator)
- ⚠️ Floating-point (stored as 64-bit integers)

### Not Supported
- ❌ File I/O (beyond stdout/stderr)
- ❌ Network operations
- ❌ Multi-threading
- ❌ External libraries
- ❌ Inline assembly (x86 specific)

## Example Programs

### Hello World

```hylian
fn main() {
    println("Hello, Termina!");
    return 0;
}
```

### Function Call

```hylian
fn add(a: int, b: int) -> int {
    return a + b;
}

fn main() {
    int x = 10;
    int y = 32;
    int sum = add(x, y);
    return sum;
}
```

### Loop

```hylian
fn main() {
    int sum = 0;
    int i = 1;
    
    while (i <= 10) {
        sum = sum + i;
        i = i + 1;
    }
    
    return sum;  // Returns 55
}
```

## Debugging

### Trace Mode

Run the VM with `-trace` to see each instruction as it executes:

```bash
./termina-vm -trace program.bin
```

Output shows:
- Program counter (PC)
- Opcode
- Register operands
- Immediate values

**Example trace output:**
```
PC=0x10000: op=0x0f rd=0 rs1=0 rs2=0 imm=1
PC=0x10004: op=0x0f rd=1 rs1=0 rs2=0 imm=0
PC=0x10008: op=0x10 rd=1 rs1=1 rs2=0 imm=1
```

### Register Dump

After execution, the VM prints all register values:

```
r0=0x0 r1=0x1000 r2=0x14 r3=0x0 r4=0x0 r5=0x0 r6=0x1015 r7=0x202a
r8=0x0 r9=0x0 r10=0x0 r11=0x0 r12=0x0 r13=0xfffff8 r14=0x10004 r15=0x10008
```

### Common Issues

**"Invalid magic" error:**
- File is not a valid Termina binary
- Recompile with `--target termina`

**"Invalid binary: code section overflows file":**
- Corrupted binary file
- Check compiler output for errors

**No output:**
- Check that `println()` is being called
- Verify string constants are in data section
- Use `-trace` to see execution

## Runtime Support

The Termina runtime (`runtime/platform/termina.c`) provides:

- Memory allocation (simple bump allocator)
- String functions (`strlen`, `strcmp`, `strcpy`, etc.)
- Memory functions (`memcpy`, `memset`)
- Arena allocator
- Print helpers

**Note:** The runtime is compiled to Hylian IR, not to native code, so system calls must be handled by the VM's `INT` instruction.

## Implementation Details

### Codegen Pass

The Termina code generator (`compiler/codegen_termina.c`) performs:

1. **String pooling**: Collects all string constants into data section
2. **Function compilation**: Translates IR instructions to Termina bytecode
3. **Register allocation**: Maps IR temps to physical registers
4. **Label resolution**: Patches forward jumps and function calls
5. **Binary emission**: Writes TERM format file

### Register Allocation

Simple strategy:
- IR temporaries map to `r0-r12`
- If registers exhausted, spills to stack
- Scratch register (`r12`) for complex operations

### Optimizations

Currently minimal:
- Dead code elimination (inherited from IR optimizer)
- Constant folding (in IR)
- No bytecode-specific optimizations yet

## Performance

Termina is an **interpreted VM**, so performance is slower than native code:

- ~10-100x slower than native (depending on workload)
- Good for learning, prototyping, and portability
- Not suitable for performance-critical applications

Future enhancements could include:
- JIT compilation
- Threaded interpreter
- Specialized instructions

## Use Cases

Termina is ideal for:

- **Learning**: Understanding compilation and VM design
- **Prototyping**: Quick iteration without native toolchain
- **Portability**: Run on any platform with a VM
- **Sandboxing**: Isolated execution environment
- **Embedded systems**: Small, simple VM for resource-constrained devices

## Building the VM

### Prerequisites
- C++11 compatible compiler (g++, clang++, MSVC)

### Compilation

```bash
cd Termina/emu
g++ -std=c++11 main.cpp -o termina-vm
```

### Installation

Copy the VM to your PATH:

```bash
sudo cp termina-vm /usr/local/bin/
```

Or add to your shell configuration:

```bash
export PATH="$PATH:/path/to/Hylian/Termina/emu"
```

## Future Enhancements

Planned improvements:

- [ ] More system calls (read, open, close, etc.)
- [ ] Floating-point instructions
- [ ] Memory-mapped I/O
- [ ] Exception handling
- [ ] Debugger interface (breakpoints, step, inspect)
- [ ] JIT compilation for hot code paths
- [ ] Bytecode verification
- [ ] Optimized interpreter (threaded dispatch)

## Resources

- [Termina VM README](../../Termina/README.md)
- [Instruction Set Architecture](../../Termina/docs/ISA.md)
- [VM Implementation](../../Termina/emu/main.cpp)
- [Code Generator](../../compiler/codegen_termina.c)
- [Runtime Support](../../runtime/platform/termina.c)

## Contributing

To contribute to the Termina backend:

1. Study the ISA documentation
2. Review the code generator implementation
3. Test with the provided examples
4. Submit improvements via pull request

Areas needing work:
- More comprehensive runtime library
- Better floating-point support
- Optimizations in code generator
- Additional system calls
- Better error messages

## License

Part of the Hylian programming language project.