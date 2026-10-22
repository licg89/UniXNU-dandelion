#!/usr/bin/env python3
"""
Android boot.img creator for MTK devices

Supports header version 2 (required by MTK LK)
Format: header + kernel + ramdisk + second stage + recovery_dtbo + dtb
"""

import struct
import sys
import os
import argparse

def round_to_page(size, page_size=2048):
    """Round size up to page boundary"""
    return (size + page_size - 1) & ~(page_size - 1)

def create_boot_img(kernel_path, output_path, base_addr=0x40000000,
                    kernel_offset=0x00080000, ramdisk_offset=0x01000000,
                    second_offset=0x00000000, tags_offset=0x00000100,
                    dtb_offset=0x07808000, page_size=2048,
                    cmdline="xnu_chainloader", header_version=0,
                    ramdisk_path=None, dtb_path=None, os_version=0):
    """Create Android boot.img"""

    # Read kernel binary
    with open(kernel_path, 'rb') as f:
        kernel_data = f.read()

    kernel_size = len(kernel_data)
    kernel_addr = base_addr + kernel_offset

    # Read ramdisk if provided
    ramdisk_data = b''
    ramdisk_size = 0
    if ramdisk_path and os.path.exists(ramdisk_path):
        with open(ramdisk_path, 'rb') as f:
            ramdisk_data = f.read()
        ramdisk_size = len(ramdisk_data)
    ramdisk_addr = base_addr + ramdisk_offset

    # Read DTB if provided
    dtb_data = b''
    dtb_size = 0
    if dtb_path and os.path.exists(dtb_path):
        with open(dtb_path, 'rb') as f:
            dtb_data = f.read()
        dtb_size = len(dtb_data)

    # Addresses
    second_size = 0
    second_addr = base_addr + second_offset if second_offset != 0 else 0
    tags_addr = base_addr + tags_offset
    dtb_addr = base_addr + dtb_offset

    # Header fields
    magic = b'ANDROID!'
    name = b'\x00' * 16  # Must be all zeros like original
    cmdline_bytes = cmdline.encode('ascii')[:512]
    cmdline_bytes = cmdline_bytes.ljust(512, b'\x00')
    extra_cmdline = b'\x00' * 1024
    id_data = struct.pack('<8I', 0, 0, 0, 0, 0, 0, 0, 0)

    # Build header based on version
    if header_version == 0:
        # Version 0 header
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
                            os_version,
                            name,
                            cmdline_bytes)
        header += id_data
    elif header_version == 2:
        # Version 2 header (for MTK)
        recovery_dtbo_size = 0
        recovery_dtbo_offset = 0
        header_size = 1648  # Size of header up to dtb_size

        header = struct.pack('<8sIIIIIIII',
                            magic,
                            kernel_size,
                            kernel_addr,
                            ramdisk_size,
                            ramdisk_addr,
                            second_size,
                            second_addr,
                            tags_addr,
                            page_size)
        header += struct.pack('<I', header_version)  # header_version = 2
        header += struct.pack('<I', os_version)  # os_version
        header += name  # 16 bytes
        header += cmdline_bytes  # 512 bytes
        header += id_data  # 32 bytes
        header += extra_cmdline  # 1024 bytes
        header += struct.pack('<I', header_size)  # header_size
        header += struct.pack('<I', recovery_dtbo_size)  # recovery_dtbo_size
        header += struct.pack('<I', recovery_dtbo_offset)  # recovery_dtbo_offset
        header += struct.pack('<I', dtb_size if dtb_size else 0)  # dtb_size
        header += struct.pack('<I', dtb_addr if dtb_size else 0)  # dtb_addr
    else:
        print(f"ERROR: Unsupported header version {header_version}")
        sys.exit(1)

    # Pad header to page size
    header_padded = header.ljust(round_to_page(len(header), page_size), b'\x00')

    # Pad kernel to page size
    kernel_padded = kernel_data.ljust(round_to_page(kernel_size, page_size), b'\x00')

    # Pad ramdisk to page size
    ramdisk_padded = ramdisk_data.ljust(round_to_page(ramdisk_size, page_size), b'\x00') if ramdisk_size else b''

    # Pad dtb to page size
    dtb_padded = dtb_data.ljust(round_to_page(dtb_size, page_size), b'\x00') if dtb_size else b''

    # Write boot.img
    with open(output_path, 'wb') as f:
        f.write(header_padded)
        f.write(kernel_padded)
        if ramdisk_size:
            f.write(ramdisk_padded)
        if dtb_size:
            f.write(dtb_padded)

    print(f"Created boot.img: {output_path}")
    print(f"  Header version: {header_version}")
    print(f"  Kernel size: {kernel_size} bytes")
    print(f"  Ramdisk size: {ramdisk_size} bytes")
    print(f"  DTB size: {dtb_size} bytes")
    print(f"  Total size: {os.path.getsize(output_path)} bytes")
    print(f"  Base address: 0x{base_addr:08X}")
    print(f"  Kernel address: 0x{kernel_addr:08X}")

def parse_int(s):
    """Parse integer from string (supports hex with 0x prefix)"""
    return int(s, 0)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Create Android boot.img')
    parser.add_argument('--kernel', required=True, help='Kernel binary path')
    parser.add_argument('--output', required=True, help='Output boot.img path')
    parser.add_argument('--cmdline', default='xnu_chainloader',
                       help='Kernel cmdline (default: xnu_chainloader)')
    parser.add_argument('--base', type=parse_int, default=0x40000000,
                       help='Base address (default: 0x40000000)')
    parser.add_argument('--kernel_offset', type=parse_int, default=0x00080000,
                       help='Kernel offset (default: 0x00080000)')
    parser.add_argument('--ramdisk_offset', type=parse_int, default=0x01000000,
                       help='Ramdisk offset (default: 0x01000000)')
    parser.add_argument('--second_offset', type=parse_int, default=0x00000000,
                       help='Second stage offset (default: 0x00000000)')
    parser.add_argument('--tags_offset', type=parse_int, default=0x00000100,
                       help='Tags offset (default: 0x00000100)')
    parser.add_argument('--dtb_offset', type=parse_int, default=0x07808000,
                       help='DTB offset (default: 0x07808000)')
    parser.add_argument('--os_version', type=parse_int, default=0x00000000,
                       help='OS version (default: 0x00000000)')
    parser.add_argument('--pagesize', type=int, default=2048,
                       help='Page size (default: 2048)')
    parser.add_argument('--header_version', type=int, default=0, choices=[0, 2],
                       help='Header version (default: 0, MTK requires 2)')
    parser.add_argument('--ramdisk', help='Ramdisk path (optional)')
    parser.add_argument('--dtb', help='DTB path (optional, for header_version 2)')

    args = parser.parse_args()

    if not os.path.exists(args.kernel):
        print(f"ERROR: Kernel not found: {args.kernel}")
        sys.exit(1)

    create_boot_img(args.kernel, args.output,
                   base_addr=args.base,
                   kernel_offset=args.kernel_offset,
                   ramdisk_offset=args.ramdisk_offset,
                   second_offset=args.second_offset,
                   tags_offset=args.tags_offset,
                   dtb_offset=args.dtb_offset,
                   page_size=args.pagesize,
                   cmdline=args.cmdline,
                   header_version=args.header_version,
                   ramdisk_path=args.ramdisk,
                   dtb_path=args.dtb,
                   os_version=args.os_version)
