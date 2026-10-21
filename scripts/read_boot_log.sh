#!/bin/bash
#
# Read boot logs from device
# Logs are stored in boot partition at offset 4MB
#

echo "========================================"
echo "  Reading Boot Logs from Device"
echo "========================================"
echo ""

# Check ADB
if ! adb devices | grep -q "device$"; then
    echo "ERROR: No device connected"
    exit 1
fi

echo "[1] Reading log from boot partition..."
echo "    Offset: 4MB (block 8192)"
echo "    Size: 4KB"
echo ""

# Read log from boot partition
# Boot partition is at /dev/block/bootdevice/by-name/boot
# Log is at offset 0x400000 (4MB)
adb shell su -c "dd if=/dev/block/bootdevice/by-name/boot bs=4096 skip=1024 count=1 2>/dev/null" | strings

echo ""
echo "========================================"
echo "  Log Reading Complete"
echo "========================================"
echo ""
echo "If no output, logs may have been overwritten by Android boot."
echo "Try reading immediately after test kernel runs."
