#!/bin/bash
#
# Read boot log from userdata partition
#

echo "========================================"
echo "  Reading Boot Log from Device"
echo "========================================"
echo ""

# Check ADB
if ! adb devices | grep -q "device$"; then
    echo "ERROR: No device connected"
    exit 1
fi

echo "[1] Reading log from userdata..."
echo "    Location: 256MB offset"
echo "    Size: 8KB"
echo ""

# Read log from userdata partition
adb shell su -c "dd if=/dev/block/bootdevice/by-name/userdata bs=4096 skip=65536 count=2 2>/dev/null" > /tmp/boot_log.raw 2>&1

if [ $? -eq 0 ]; then
    echo "Log saved to /tmp/boot_log.raw"
    echo ""
    echo "Contents:"
    echo "--------"
    cat -v /tmp/boot_log.raw | head -50
    echo "--------"
else
    echo "Failed to read log"
fi

echo ""
echo "========================================"
echo "  Complete"
echo "========================================"
