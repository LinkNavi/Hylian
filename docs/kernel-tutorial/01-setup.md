# 1. Setting Up Your Environment

A kernel isn't a normal program. It has no operating system underneath it —
no libc, no `malloc`, no file descriptors, nothing — because it *is* what
provides all of that to everything else. Building and running one needs a
few extra pieces beyond the normal Hylian toolchain:

| Tool | What it's for |
|---|---|
| `hylian` | The Hylian compiler itself |
| `zora` | Hylian's build tool — drives the freestanding build for you |
| [Limine](https://github.com/limine-bootloader/limine) | The bootloader that loads your kernel and hands off control to it |
| `xorriso` | Builds a bootable ISO image out of your kernel binary |
| `qemu` | An emulator, so you can boot your kernel without touching real hardware |

## Why a bootloader?

When a PC powers on, firmware (BIOS or UEFI) doesn't know anything about
your kernel — it knows how to load a small program from disk and jump to
it, and that's it. That small program is the **bootloader**. Its job is to:

1. Get the CPU into a sane state (64-bit long mode, in Limine's case)
2. Load your kernel binary into memory
3. Build a **memory map** describing what physical memory exists and what's
   already in use (you'll need this in [Chapter 5](05-pmm.md))
4. Jump to your kernel's entry point

Writing your own bootloader is a whole project in itself and mostly
boilerplate that has nothing to do with kernel logic, so this tutorial uses
Limine, a mature, widely-used bootloader that Hylian has first-class support
for via the `--target limine` compiler flag.

## Installing the toolchain

### Hylian and Zora

If you're reading this from inside the Hylian source tree, you likely
already have both built. Otherwise follow the main
[install instructions](../README.md). This tutorial assumes both `hylian`
and `zora` are on your `PATH`.

Confirm the kernel-dev runtime support is actually installed — this is
the one piece that's easy to miss, since it isn't part of the ordinary
`stdlib/` install:

```sh
ls "$(dirname "$(command -v hylian)")/../lib/hylian/std/platform/"
# should show: limine.o  limine.ld  linux_x86_64.hy  (and kernel.o)
```

If `limine.o`/`limine.ld` aren't there, re-run the installer
(`./devInstall.sh`, from the Hylian source tree) — it builds and installs
them as part of the normal install now. If you're on an older checkout
that doesn't do this, build and copy them by hand:

```sh
gcc -c runtime/platform/limine.c -o runtime/platform/limine.o \
    -ffreestanding -fno-stack-protector -fno-stack-check \
    -mno-red-zone -mcmodel=kernel -fno-pic
cp runtime/platform/limine.o runtime/platform/limine.ld \
   /usr/local/lib/hylian/std/platform/
```

### Limine

Install it via your package manager if it's available (`limine` on Arch,
for instance). Otherwise, clone and build it directly:

```sh
git clone https://github.com/limine-bootloader/limine.git --branch=v9.x-binary --depth=1
cd limine
make
```

Either way, you need the `limine` CLI (used to install the bootloader onto
an ISO) and Limine's **data directory** — a handful of `.bin`/`.sys`/`.EFI`
files that get copied onto every ISO you build. Find it with:

```sh
limine --print-datadir
```

### xorriso and qemu

These are standard packages on every major distribution:

```sh
# Arch
sudo pacman -S xorriso qemu-system-x86

# Debian/Ubuntu
sudo apt install xorriso qemu-system-x86
```

## Building a bootable ISO

`zora build` compiles your kernel and links it against the Limine runtime
object, producing a plain ELF executable — but turning that into bootable
media is Limine's job, not Hylian's or Zora's, so there's no single command
for it. The following script does it: it stages the kernel binary plus
Limine's own boot files into a directory tree, calls `xorriso` to pack that
into an ISO, then calls `limine bios-install` to write BIOS boot code into
the ISO's boot sector.

Save this as `make-iso.sh` next to your kernel project and adjust
`KERNEL_ELF` to wherever your build puts the binary:

```sh
#!/bin/sh
set -e

KERNEL_ELF="zora-build/bin/mykernel"   # adjust to your target name
LIMINE_DATADIR="$(limine --print-datadir)"
ISO_ROOT="iso_root"
OUT="mykernel.iso"

rm -rf "$ISO_ROOT"
mkdir -p "$ISO_ROOT/boot/limine"

cp "$KERNEL_ELF" "$ISO_ROOT/boot/kernel.elf"
cp "$LIMINE_DATADIR"/limine-bios.sys \
   "$LIMINE_DATADIR"/limine-bios-cd.bin \
   "$LIMINE_DATADIR"/limine-uefi-cd.bin \
   "$ISO_ROOT/boot/limine/"

cat > "$ISO_ROOT/boot/limine.conf" <<EOF
timeout: 0

/My Kernel
    protocol: limine
    kernel_path: boot():/boot/kernel.elf
EOF

xorriso -as mkisofs -b boot/limine/limine-bios-cd.bin \
    -no-emul-boot -boot-load-size 4 -boot-info-table \
    --efi-boot boot/limine/limine-uefi-cd.bin \
    -efi-boot-part --efi-boot-image --protective-msdos-label \
    "$ISO_ROOT" -o "$OUT"

limine bios-install "$OUT"

echo "Built $OUT"
```

```sh
chmod +x make-iso.sh
```

`limine.conf` is Limine's own boot menu configuration — `timeout: 0` skips
the menu and boots immediately, and the `protocol: limine` line tells
Limine to use its native boot protocol (as opposed to Multiboot or a raw
chainload) when jumping into `kernel.elf`. You won't need to touch this
file again for the rest of the tutorial.

## Booting in QEMU

```sh
qemu-system-x86_64 -cdrom mykernel.iso -serial stdio
```

`-serial stdio` routes the kernel's serial output to your terminal — useful
because `println`/`vga_print` write to *both* the VGA framebuffer and the
serial port under `--target limine` (see [Runtime
support](../language/kernel.md#runtime-support)), so you can see output
without a graphical QEMU window at all. Add `-display none` if you want
serial-only output with no window.

Two flags worth knowing while debugging:

- `-no-reboot` — stop QEMU instead of rebooting when the kernel triple-faults
  (crashes badly enough to reset the CPU). Without this, a crash just
  reboots forever, which looks identical to a hang.
- `-d int -D qemu.log` — log every interrupt and CPU exception QEMU
  delivers to `qemu.log`. Indispensable once you get to
  [Chapter 4](04-idt-interrupts.md) — a triple fault with no explanation is
  one of the most common kernel-dev experiences, and this log is how you
  find out *which* exception actually happened first.

With the toolchain in place, [Chapter 2](02-hello-kernel.md) builds the
smallest possible kernel and boots it.
