#!/bin/bash
# Boots small freestanding kernels (tests/kernel/*.hy) in QEMU through Limine and checks
# what they print on the serial port. Covers the freestanding backend: static arrays,
# port I/O, a naked ISR with save_regs/iret driven by the PIT, and that the optimizer
# treats hlt/sti/port I/O as memory barriers.
#
# Needs: qemu-system-x86_64, xorriso, limine (+ /usr/share/limine/limine-bios*.{bin,sys}),
# and the Zora build tool at ../Zora/build/zora or $ZORA. Skips (exit 0) when any is missing.
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ZORA="${ZORA:-$ROOT/../Zora/build/zora}"
for t in qemu-system-x86_64 xorriso limine; do
    command -v $t >/dev/null || { echo "kernel tests: $t not found - skipped"; exit 0; }
done
[ -x "$ZORA" ] && [ -f /usr/share/limine/limine-bios-cd.bin ] || { echo "kernel tests: zora/limine files missing - skipped"; exit 0; }
[ -x "$ROOT/hylian" ] || { echo "compiler not built: run ./build.sh first"; exit 1; }

W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
mkdir -p "$W/std/platform"
cp -r "$ROOT/stdlib/." "$W/std/"
gcc -c "$ROOT/runtime/platform/limine.c" -o "$W/std/platform/limine.o" -ffreestanding -fno-stack-protector \
    -fno-stack-check -mno-red-zone -mcmodel=kernel -fno-pic || exit 1
cp "$ROOT/runtime/platform/limine.ld" "$W/std/platform/"
PASS=0; FAIL=0
for src in "$ROOT"/tests/kernel/*.hy; do
    name=$(basename "$src" .hy)
    P="$W/$name"; mkdir -p "$P/src" "$P/iso/boot/limine"
    cp "$src" "$P/src/main.hy"
    printf '[project]\nname = "k"\nversion = "0.1.0"\n\n[target.k]\ntype = "kernel"\nsources = ["src/main.hy"]\n' > "$P/Zora.toml"
    ( cd "$P" && HYLIAN_BIN="$ROOT/hylian" HYLIAN_LIB="$W/std" "$ZORA" build >"$P/build.log" 2>&1 ) || { echo "  ✗ FAIL  $name (build)"; tail -5 "$P/build.log"; FAIL=$((FAIL+1)); continue; }
    cp "$P/zora-build/bin/k" "$P/iso/boot/k"
    printf 'timeout: 0\n/k\n    protocol: limine\n    kernel_path: boot():/boot/k\n' > "$P/iso/boot/limine/limine.conf"
    cp /usr/share/limine/limine-bios-cd.bin /usr/share/limine/limine-bios.sys "$P/iso/boot/limine/"
    xorriso -as mkisofs -b boot/limine/limine-bios-cd.bin -no-emul-boot -boot-load-size 4 -boot-info-table \
        "$P/iso" -o "$P/k.iso" >/dev/null 2>&1
    limine bios-install "$P/k.iso" >/dev/null 2>&1
    timeout 15 qemu-system-x86_64 -cdrom "$P/k.iso" -serial stdio -display none -no-reboot -m 128 2>/dev/null \
        | tr -d '\r' > "$P/out.txt"
    if diff <(head -c 4000 "$P/out.txt") "$ROOT/tests/kernel/$name.expected" >/dev/null; then
        echo "  ✓ PASS  kernel $name"; PASS=$((PASS+1))
    else
        echo "  ✗ FAIL  kernel $name"; diff "$P/out.txt" "$ROOT/tests/kernel/$name.expected" | head -8; FAIL=$((FAIL+1))
    fi
done
echo "kernel tests: $PASS passed, $FAIL failed"
[ $FAIL -eq 0 ]
