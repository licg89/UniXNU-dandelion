#!/usr/bin/env python3
"""
Android boot.img creator for XNU Chainloader

This creates a standard Android boot.img that LK can load.
Format: header + kernel + ramdisk + second stage + dtb
"""

import struct
import sys
import os

def round_to_page(size, page_size=2048):
    """Round size up to page boundary"""
    return (size + page_size - 1) & ~(page_size - 1)

def create_boot_img(kernel_path, output_path, base_addr=0x40000000,
                    page_size=2048, cmdline="xnu_chainloader"):
    """Create Android boot.img"""

    # Read kernel binary
    with open(kernel_path, 'rb') as f:
        kernel_data = f.read()

    kernel_size = len(kernel_data)

    # Create header
    # Android boot image header format (version 0)
    magic = b'ANDROID!'
    kernel_addr = base_addr
    ramdisk_size = 0
    ramdisk_addr = base_addr + 0x01000000
    second_size = 0
    second_addr = base_addr + 0x00F00000
    tags_addr = base_addr + 0x00000100
    unused = 0
    name = b'XNU_Chainloader\x00'
    name = name[:16].ljust(16, b'\x00')
    cmdline_bytes = cmdline.encode('ascii')[:512]
    cmdline_bytes = cmdline_bytes.ljust(512, b'\x00')

    # Calculate ID (dummy for now)
    id_data = struct.pack('<8I', 0, 0, 0, 0, 0, 0, 0, 0)

    # Pack header
    # Format: magic(8s) + 9 uint32(I) + name(16s) + cmdline(512s)
    header = struct.pack('<8sIIIIIIIII16s512s',
                        magic,
                        kernel_size,
                        kernel_addr,
                        ramdisk_size,
                        ramdisk_addr,
                        second_size,
                        second_addr,
                        tags_addr,
                        page_size,
                        0,  # unused (was os_version)
                        name,
                        cmdline_bytes)

    # Add ID to header
    header += id_data

    # Pad header to page size
    header_padded = header.ljust(round_to_page(len(header), page_size), b'\x00')

    # Pad kernel to page size
    kernel_padded = kernel_data.ljust(round_to_page(kernel_size, page_size), b'\x00')

    # Write boot.img
    with open(output_path, 'wb') as f:
        f.write(header_padded)
        f.write(kernel_padded)

    print(f"Created boot.img: {output_path}")
    print(f"  Kernel size: {kernel_size} bytes")
    print(f"  Total size: {os.path.getsize(output_path)} bytes")
    print(f"  Base address: 0x{base_addr:08X}")
    print(f"  Kernel address: 0x{kernel_addr:08X}")

if __name__ == '__main__':
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <kernel.bin> <output.img>")
        print(f"Example: {sys.argv[0]} chainloader.bin xnu_chainloader.img")
        sys.exit(1)

    kernel_path = sys.argv[1]
    output_path = sys.argv[2]

    if not os.path.exists(kernel_path):
        print(f"ERROR: Kernel not found: {kernel_path}")
        sys.exit(1)

    create_boot_img(kernel_path, output_path)
