#!/bin/bash
#
# Build script for XNU Chainloader boot.img
#
# This script:
# 1. Builds the chainloader
# 2. Packages it into Android boot.img format
# 3. Creates flashable image
#

set -e

echo "=========================================="
echo "  XNU Chainloader Boot Image Builder"
echo "=========================================="
echo ""

# Check toolchain
if ! command -v aarch64-linux-gnu-gcc &> /dev/null; then
    echo "ERROR: aarch64-linux-gnu-gcc not found"
    echo "Install with: sudo apt-get install gcc-aarch64-linux-gnu"
    exit 1
fi

# Build chainloader
echo "[1/3] Building chainloader..."
cd bootloader/chainloader
make clean
make
cd ../..

# Check output
if [ ! -f bootloader/chainloader/chainloader.bin ]; then
    echo "ERROR: chainloader.bin not found"
    exit 1
fi

echo "[2/3] Chainloader built successfully"
ls -lh bootloader/chainloader/chainloader.bin

# Create boot.img
echo "[3/3] Creating Android boot.img..."

# Check if mkbootimg is available
if command -v mkbootimg &> /dev/null; then
    mkbootimg \
        --kernel bootloader/chainloader/chainloader.bin \
        --base 0x40000000 \
        --kernel_offset 0x00000000 \
        --ramdisk_offset 0x05000000 \
        --second_offset 0x00F00000 \
        --tags_offset 0x04000000 \
        --pagesize 2048 \
        --cmdline "xnu_chainloader" \
        --output xnu_chainloader.img

    echo "boot.img created successfully!"
    ls -lh xnu_chainloader.img
else
    echo "WARNING: mkbootimg not found"
    echo ""
    echo "To create boot.img manually, install Android tools:"
    echo "  sudo apt-get install android-tools-fsutils"
    echo ""
    echo "Or use Python script:"
    echo "  python3 scripts/mkbootimg.py \\"
    echo "    --kernel bootloader/chainloader/chainloader.bin \\"
    echo "    --output xnu_chainloader.img"
    echo ""
    echo "Raw binary available at:"
    ls -lh bootloader/chainloader/chainloader.bin
fi

echo ""
echo "=========================================="
echo "  Build Complete!"
echo "=========================================="
echo ""
echo "Next steps:"
echo "  1. Flash to device:"
echo "     fastboot flash boot xnu_chainloader.img"
echo "     fastboot reboot"
echo ""
echo "  2. Connect UART to see boot logs"
echo ""
echo "  3. Make sure XNU kernel is at eMMC offset 0x4000000"
echo ""
