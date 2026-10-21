#!/bin/bash
#
# QEMU Test Script for XNU on MT6765
#
# Tests the boot code in QEMU before real hardware testing
#

set -e

# Configuration
QEMU=qemu-system-aarch64
KERNEL=boot_mt6765.bin
DTB=mt6765-dandelion.dtb
MEMORY=4G
CPUS=4

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}=== XNU on MT6765 - QEMU Test ===${NC}"
echo ""

# Check if kernel exists
if [ ! -f "$KERNEL" ]; then
    echo -e "${YELLOW}Kernel not found. Building...${NC}"
    make boot || {
        echo -e "${RED}Build failed!${NC}"
        exit 1
    }
fi

# Check if DTB exists
if [ ! -f "$DTB" ]; then
    echo -e "${YELLOW}Device tree not found. Building...${NC}"
    make dtb || {
        echo -e "${RED}DTB build failed!${NC}"
        exit 1
    }
fi

echo -e "${GREEN}Configuration:${NC}"
echo "  QEMU: $QEMU"
echo "  Kernel: $KERNEL"
echo "  DTB: $DTB"
echo "  Memory: $MEMORY"
echo "  CPUs: $CPUS"
echo ""

# Launch QEMU
echo -e "${GREEN}Launching QEMU...${NC}"
echo ""

$QEMU \
    -M virt \
    -cpu cortex-a53 \
    -smp $CPUS \
    -m $MEMORY \
    -kernel $KERNEL \
    -dtb $DTB \
    -nographic \
    -serial mon:stdio \
    -append "debug=0x8 loglevel=15 earlycon" \
    -net none \
    -no-reboot

echo ""
echo -e "${GREEN}=== QEMU Test Complete ===${NC}"
