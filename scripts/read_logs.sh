#!/bin/bash
#
# Create log partition and read logs
#

echo "========================================"
echo "  Boot Log Reader"
echo "========================================"
echo ""

# Check ADB
if ! adb devices | grep -q "device$"; then
    echo "ERROR: No device connected"
    exit 1
fi

echo "[1] Checking log partitions..."
echo ""

# Try to read from different locations
echo "Trying boot partition header..."
adb shell su -c "dd if=/dev/block/bootdevice/by-name/boot bs=512 skip=1 count=8 2>/dev/null | strings | head -20"

echo ""
echo "Trying misc partition..."
adb shell su -c "dd if=/dev/block/bootdevice/by-name/misc bs=4096 count=1 2>/dev/null | strings | head -20"

echo ""
echo "Trying persist partition..."
adb shell su -c "dd if=/dev/block/bootdevice/by-name/persist bs=4096 count=1 2>/dev/null | strings | head -20"

echo ""
echo "========================================"
echo "  If no logs found, try:"
echo "  1. Flash test kernel again"
echo "  2. Wait 10 seconds"
echo "  3. Immediately restore and read"
echo "========================================"
