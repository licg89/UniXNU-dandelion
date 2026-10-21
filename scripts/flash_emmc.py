#!/usr/bin/env python3
"""
eMMC Flash Tool for XNU Chainloader

This tool writes the XNU kernel and DTB to specific offsets in the eMMC
boot partition. It can work with:
1. A raw eMMC dump file (for testing)
2. A real device via fastboot/ADB (requires root)

Usage:
    python3 flash_emmc.py --emmc emmc_dump.img --xnu xnu_kernel.bin --dtb dandelion.dtb
"""

import argparse
import os
import struct

# eMMC partition layout (must match chainloader.h)
XNU_BOOT_PART_OFFSET = 0x4000000   # 64MB
XNU_BOOT_PART_SIZE   = 0x4000000   # 64MB for XNU kernel
DTB_PART_OFFSET      = 0x8000000   # 128MB
DTB_PART_SIZE        = 0x100000    # 1MB for DTB

def write_to_emmc_dump(emmc_path, xnu_path=None, dtb_path=None):
    """Write XNU and DTB to eMMC dump file"""

    # Get eMMC size
    emmc_size = os.path.getsize(emmc_path)
    print(f"eMMC size: {emmc_size} bytes ({emmc_size // (1024*1024)} MB)")

    # Check if offsets are valid
    if XNU_BOOT_PART_OFFSET + XNU_BOOT_PART_SIZE > emmc_size:
        print(f"ERROR: XNU partition offset exceeds eMMC size")
        return False

    if dtb_path and DTB_PART_OFFSET + DTB_PART_SIZE > emmc_size:
        print(f"ERROR: DTB partition offset exceeds eMMC size")
        return False

    # Open eMMC for read/write
    with open(emmc_path, 'r+b') as f:
        # Write XNU kernel
        if xnu_path:
            print(f"\nWriting XNU kernel...")
            xnu_data = open(xnu_path, 'rb').read()
            xnu_size = len(xnu_data)
            print(f"  Source: {xnu_path} ({xnu_size} bytes)")
            print(f"  Offset: 0x{XNU_BOOT_PART_OFFSET:08X}")

            if xnu_size > XNU_BOOT_PART_SIZE:
                print(f"  ERROR: XNU kernel too large ({xnu_size} > {XNU_BOOT_PART_SIZE})")
                return False

            f.seek(XNU_BOOT_PART_OFFSET)
            f.write(xnu_data)
            print(f"  OK: Wrote {xnu_size} bytes")

        # Write DTB
        if dtb_path:
            print(f"\nWriting DTB...")
            dtb_data = open(dtb_path, 'rb').read()
            dtb_size = len(dtb_data)
            print(f"  Source: {dtb_path} ({dtb_size} bytes)")
            print(f"  Offset: 0x{DTB_PART_OFFSET:08X}")

            if dtb_size > DTB_PART_SIZE:
                print(f"  ERROR: DTB too large ({dtb_size} > {DTB_PART_SIZE})")
                return False

            f.seek(DTB_PART_OFFSET)
            f.write(dtb_data)
            print(f"  OK: Wrote {dtb_size} bytes")

    return True

def verify_emmc_dump(emmc_path):
    """Verify XNU and DTB are present in eMMC dump"""
    print(f"\nVerifying eMMC contents...")

    with open(emmc_path, 'rb') as f:
        # Check XNU kernel
        f.seek(XNU_BOOT_PART_OFFSET)
        xnu_header = f.read(16)
        xnu_valid = xnu_header != b'\x00' * 16 and xnu_header != b'\xFF' * 16

        if xnu_valid:
            print(f"  XNU kernel: Present at 0x{XNU_BOOT_PART_OFFSET:08X}")
            # Try to find size
            f.seek(XNU_BOOT_PART_OFFSET)
            data = f.read(0x1000)  # Read first 4KB
            # Find last non-zero byte
            size = 0x1000
            for i in range(len(data)-1, -1, -1):
                if data[i] != 0:
                    size = i + 1
                    break
            print(f"    Estimated size: {size} bytes")
        else:
            print(f"  XNU kernel: NOT FOUND")

        # Check DTB
        f.seek(DTB_PART_OFFSET)
        dtb_header = f.read(16)
        dtb_valid = dtb_header != b'\x00' * 16 and dtb_header != b'\xFF' * 16

        if dtb_valid:
            print(f"  DTB: Present at 0x{DTB_PART_OFFSET:08X}")
        else:
            print(f"  DTB: NOT FOUND")

    return xnu_valid

def main():
    parser = argparse.ArgumentParser(description='Flash XNU to eMMC')
    parser.add_argument('--emmc', required=True, help='eMMC dump file')
    parser.add_argument('--xnu', help='XNU kernel binary')
    parser.add_argument('--dtb', help='Device tree blob')
    parser.add_argument('--verify', action='store_true', help='Verify only')
    args = parser.parse_args()

    if not os.path.exists(args.emmc):
        print(f"ERROR: eMMC file not found: {args.emmc}")
        return 1

    if args.verify:
        verify_emmc_dump(args.emmc)
        return 0

    if not args.xnu:
        print("ERROR: --xnu required unless --verify is used")
        return 1

    print("==========================================")
    print("  XNU eMMC Flash Tool")
    print("==========================================")

    success = write_to_emmc_dump(args.emmc, args.xnu, args.dtb)

    if success:
        print("\n==========================================")
        print("  Flash Complete!")
        print("==========================================")
        print("\nTo flash to real device:")
        print("  1. Extract the modified eMMC partition")
        print("  2. Use: fastboot flash boot <partition.img>")
        print("  3. Or use SP Flash Tool for full eMMC write")
        verify_emmc_dump(args.emmc)
        return 0
    else:
        print("\nFlash FAILED")
        return 1

if __name__ == '__main__':
    exit(main())
