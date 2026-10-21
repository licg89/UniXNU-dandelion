#!/bin/bash
echo "Extract boot.img from device (requires root):"
echo ""
echo "  adb shell su -c 'dd if=/dev/block/by-name/boot of=/sdcard/boot.img'"
echo "  adb pull /sdcard/boot.img"
echo ""
echo "Then analyze with:"
echo "  unpackbootimg -i boot.img"
echo "  hexdump -C boot.img | head -50"
