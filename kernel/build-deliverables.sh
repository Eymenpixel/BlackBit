#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
KERNEL="$ROOT/kernel"
OUT="${1:-/mnt/c/Users/srhtm/Downloads/biso}"
A64_AS="${A64_AS:-aarch64-linux-gnu-as}"
A64_LD="${A64_LD:-aarch64-linux-gnu-ld}"
A64_OBJCOPY="${A64_OBJCOPY:-aarch64-linux-gnu-objcopy}"
ARM32_AS="${ARM32_AS:-arm-linux-gnueabihf-as}"
ARM32_LD="${ARM32_LD:-arm-linux-gnueabihf-ld}"
ARM32_OBJCOPY="${ARM32_OBJCOPY:-arm-linux-gnueabihf-objcopy}"
LOCAL_CROSS="$ROOT/work/cross-tools"
if [[ ! -x "$(command -v "$A64_AS" 2>/dev/null || true)" && -x "$LOCAL_CROSS/a64/usr/bin/aarch64-linux-gnu-as" ]]; then
    A64_AS="$LOCAL_CROSS/a64/usr/bin/aarch64-linux-gnu-as"
    A64_LD="$LOCAL_CROSS/a64/usr/bin/aarch64-linux-gnu-ld"
    A64_OBJCOPY="$LOCAL_CROSS/a64/usr/bin/aarch64-linux-gnu-objcopy"
    export LD_LIBRARY_PATH="$LOCAL_CROSS/a64/usr/lib/x86_64-linux-gnu:$LOCAL_CROSS/arm32/usr/lib/x86_64-linux-gnu:${LD_LIBRARY_PATH:-}"
fi
if [[ ! -x "$(command -v "$ARM32_AS" 2>/dev/null || true)" && -x "$LOCAL_CROSS/arm32/usr/bin/arm-linux-gnueabihf-as" ]]; then
    ARM32_AS="$LOCAL_CROSS/arm32/usr/bin/arm-linux-gnueabihf-as"
    ARM32_LD="$LOCAL_CROSS/arm32/usr/bin/arm-linux-gnueabihf-ld"
    ARM32_OBJCOPY="$LOCAL_CROSS/arm32/usr/bin/arm-linux-gnueabihf-objcopy"
    export LD_LIBRARY_PATH="$LOCAL_CROSS/a64/usr/lib/x86_64-linux-gnu:$LOCAL_CROSS/arm32/usr/lib/x86_64-linux-gnu:${LD_LIBRARY_PATH:-}"
fi

mkdir -p "$OUT"
make -C "$KERNEL" iso boot-image iso-x64 test test-x64
cp "$KERNEL/build/blackbit-ui.iso" "$OUT/blackbit-x32.iso"
cp "$KERNEL/build/blackbit-bios.img" "$OUT/blackbit-x32.img"
cp "$KERNEL/build/blackbit-x64.iso" "$OUT/blackbit-x64.iso"
# grub-mkrescue creates an isohybrid image, so this is raw-writable as well.
cp "$KERNEL/build/blackbit-x64.iso" "$OUT/blackbit-x64.img"

mkdir -p "$KERNEL/build/arm64" "$KERNEL/build/arm32" \
    "$KERNEL/build/arm64-iso/boot" "$KERNEL/build/arm32-iso/boot"
"$A64_AS" -o "$KERNEL/build/arm64/boot.o" "$KERNEL/arch/arm64/boot.S"
"$A64_LD" -T "$KERNEL/arch/arm64/linker.ld" \
    -o "$KERNEL/build/arm64/kernel.elf" "$KERNEL/build/arm64/boot.o"
"$A64_OBJCOPY" -O binary "$KERNEL/build/arm64/kernel.elf" "$OUT/blackbit-arm64.img"
"$ARM32_AS" -o "$KERNEL/build/arm32/boot.o" "$KERNEL/arch/arm32/boot.S"
"$ARM32_LD" -T "$KERNEL/arch/arm32/linker.ld" \
    -o "$KERNEL/build/arm32/kernel.elf" "$KERNEL/build/arm32/boot.o"
"$ARM32_OBJCOPY" -O binary "$KERNEL/build/arm32/kernel.elf" "$KERNEL/build/arm32/kernel.bin"
python3 "$ROOT/tools/pack_uimage.py" "$KERNEL/build/arm32/kernel.bin" "$OUT/blackbit-arm32.img"

cp "$OUT/blackbit-arm64.img" "$KERNEL/build/arm64-iso/boot/kernel.img"
cp "$KERNEL/build/arm64/kernel.elf" "$KERNEL/build/arm64-iso/boot/kernel.elf"
cp "$OUT/blackbit-arm32.img" "$KERNEL/build/arm32-iso/boot/uImage"
cp "$KERNEL/build/arm32/kernel.elf" "$KERNEL/build/arm32-iso/boot/kernel.elf"
cat > "$KERNEL/build/arm64-iso/README.txt" <<'EOF'
Blackbit ARM64 bootstrap payload bundle.
The .img is an AArch64 Linux Image-format payload. It only initializes PL011 UART and halts.
This ISO is a data bundle, not a firmware-bootable ARM ISO.
EOF
cat > "$KERNEL/build/arm32-iso/README.txt" <<'EOF'
Blackbit ARM32 bootstrap payload bundle.
The .img is a U-Boot legacy ARM uImage. It only initializes PL011 UART and halts.
This ISO is a data bundle, not a firmware-bootable ARM ISO.
EOF
xorriso -as mkisofs -quiet -R -J -o "$OUT/blackbit-arm64.iso" "$KERNEL/build/arm64-iso"
xorriso -as mkisofs -quiet -R -J -o "$OUT/blackbit-arm32.iso" "$KERNEL/build/arm32-iso"

cat > "$OUT/README.txt" <<'EOF'
Blackbit multi-architecture build set

blackbit-x32.iso: bootable GRUB ISO for the 32-bit x86 kernel.
blackbit-x32.img: bootable raw BIOS disk image for the 32-bit x86 kernel.
blackbit-x64.iso: bootable GRUB ISO for the 64-bit x86 kernel.
blackbit-x64.img: the same isohybrid image, suitable for raw writing.
blackbit-arm64.img: AArch64 Linux Image-format bootstrap payload (UART output then halt).
blackbit-arm32.img: U-Boot legacy uImage bootstrap payload (UART output then halt).
blackbit-arm64.iso and blackbit-arm32.iso: data bundles containing the ARM payloads;
they are not bootable firmware ISOs in this build.

The ARM ports are bootstrap stubs, not the x86 command kernel. They require a
PL011-compatible target (QEMU virt for ARM64; Versatile/PL011-compatible U-Boot
board for ARM32). ARM QEMU and firmware tools were not present for runtime testing.
EOF

echo "Artifacts written to: $OUT"
