#!/bin/bash
# Build milestone 1: basic framebuffer test shim
set -euo pipefail
cd "$(dirname "$0")"
CC=aarch64-linux-gnu-gcc
OBJCOPY=aarch64-linux-gnu-objcopy
CFLAGS="-O2 -ffreestanding -nostdlib -fno-pic -mgeneral-regs-only -mstrict-align -fno-stack-protector -Wall -Wextra"
mkdir -p out

echo "=== Compiling milestone 1 (basic framebuffer test) ==="

# Compile only the basic files for milestone 1
for f in start.S shim.c console.c; do
	echo "  Compiling $f..."
	$CC $CFLAGS -c $f -o out/${f%.*}.o
done

# Check for absolute relocations
echo "  Checking relocations..."
abs=$(aarch64-linux-gnu-readelf -r out/shim.o out/console.o | grep -c ABS64 || true)
[ "$abs" = 0 ] || { echo "error: $abs absolute relocations in C objects" >&2; exit 1; }

# Link
echo "  Linking..."
$CC $CFLAGS -static -T shim.ld -Wl,--build-id=none -Wl,--no-warn-rwx-segments -Wl,-N \
	out/start.o out/shim.o out/console.o -o out/shim.elf

# Create binary image
echo "  Creating binary..."
$OBJCOPY -O binary out/shim.elf out/Image

# Pad to image_size
size=$(aarch64-linux-gnu-nm out/shim.elf | awk '$3=="_end"{print strtonum("0x"$1)}')
truncate -s "$size" out/Image

# Create empty ramdisk
echo "  Creating ramdisk..."
(cd out && rm -rf rd && mkdir rd && cd rd && find . | cpio -o -H newc --quiet | gzip -9 > ../ramdisk.bin)

# Create boot.img (use existing DTB if available, otherwise skip)
echo "  Creating boot.img..."
DTB_FILE=""
if [ -f "../build/mt6765-dandelion.dtb" ]; then
	DTB_FILE="../build/mt6765-dandelion.dtb"
	echo "  Using DTB: $DTB_FILE"
elif [ -f "../patches/dts/mt6765-dandelion.dts" ]; then
	echo "  Compiling DTB from DTS..."
	if command -v dtc >/dev/null 2>&1; then
		dtc -I dts -O dtb -o out/dandelion.dtb ../patches/dts/mt6765-dandelion.dts 2>/dev/null || true
		if [ -f out/dandelion.dtb ]; then
			DTB_FILE="out/dandelion.dtb"
		fi
	fi
fi

if [ -n "$DTB_FILE" ] && command -v mkbootimg >/dev/null 2>&1; then
	mkbootimg --header_version 2 --kernel out/Image --ramdisk out/ramdisk.bin \
		--dtb "$DTB_FILE" \
		--base 0x40000000 --kernel_offset 0x00080000 --ramdisk_offset 0x01000000 \
		--tags_offset 0x00000100 --pagesize 2048 \
		--cmdline "console=null" -o out/shim-boot.img
else
	echo "  Using Python script (no DTB)..."
	python3 ../scripts/mkbootimg.py out/Image out/shim-boot.img --base 0x40000000 --kernel_offset 0x00080000
fi

echo ""
echo "=== Milestone 1 build complete ==="
ls -lh out/Image out/shim-boot.img
echo ""
echo "Next steps:"
echo "  1. Test on device: fastboot boot out/shim-boot.img"
echo "  2. Check framebuffer output"
echo "  3. If successful, proceed to milestone 2 (MMU + GIC)"
