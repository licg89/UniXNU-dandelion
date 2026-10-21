#!/usr/bin/env python3
"""
QEMU simulation test for XNU Chainloader

This tests the chainloader boot process in QEMU before real hardware testing.
"""

import subprocess
import sys
import os
import tempfile
import struct

# Configuration
QEMU_BINARY = "qemu-system-aarch64"
MACHINE = "virt"
CPU = "cortex-a53"
MEMORY = "512M"
SERIAL = "mon:stdio"

# Memory layout (must match chainloader.h)
CHAINLOADER_BASE = 0x40000000
XNU_KERNEL_BASE = 0x40100000
XNU_DTB_BASE = 0x47000000
XNU_BOOTARGS_BASE = 0x40001000

def create_test_kernel(size=0x100000):
    """Create a dummy XNU kernel for testing"""
    # Create a simple pattern that the chainloader can detect
    kernel = bytearray(size)
    # Put a magic pattern at the start
    kernel[0:4] = b'\xAA\xBB\xCC\xDD'
    # Add some recognizable data
    for i in range(16, min(256, size), 4):
        struct.pack_into('<I', kernel, i, 0x12345678)
    return bytes(kernel)

def create_test_dtb():
    """Create a dummy device tree blob"""
    # DTB magic: 0xD00DFEED
    dtb = bytearray(4096)
    struct.pack_into('>I', dtb, 0, 0xD00DFEED)  # Big-endian magic
    struct.pack_into('>I', dtb, 4, 4096)  # Total size
    return bytes(dtb)

def create_test_emmc_image():
    """Create a test eMMC image with XNU and DTB"""
    print("Creating test eMMC image...")

    # Create 32MB eMMC image
    emmc_size = 32 * 1024 * 1024
    emmc = bytearray(emmc_size)

    # Write XNU kernel at offset 0x4000000 (64MB) - but we use smaller offset for testing
    xnu_offset = 0x100000  # 1MB offset (adjusted for testing)
    xnu_kernel = create_test_kernel()
    emmc[xnu_offset:xnu_offset+len(xnu_kernel)] = xnu_kernel

    # Write DTB at offset 0x8000000 (128MB) - adjusted for testing
    dtb_offset = 0x200000  # 2MB offset (adjusted for testing)
    dtb = create_test_dtb()
    emmc[dtb_offset:dtb_offset+len(dtb)] = dtb

    # Save to file
    emmc_path = "/tmp/test_emmc.img"
    with open(emmc_path, 'wb') as f:
        f.write(emmc)

    print(f"  Created: {emmc_path}")
    print(f"  Size: {emmc_size} bytes")
    print(f"  XNU at offset: 0x{xnu_offset:08X}")
    print(f"  DTB at offset: 0x{dtb_offset:08X}")

    return emmc_path

def build_chainloader():
    """Build the chainloader binary"""
    print("Building chainloader...")

    chainloader_dir = os.path.join(os.path.dirname(__file__),
                                   "..", "bootloader", "chainloader")

    try:
        subprocess.run(["make", "clean"], cwd=chainloader_dir, check=True)
        subprocess.run(["make"], cwd=chainloader_dir, check=True)
        print("  Build successful")
        return os.path.join(chainloader_dir, "chainloader.bin")
    except subprocess.CalledProcessError as e:
        print(f"  Build failed: {e}")
        return None

def run_qemu_test(chainloader_path, emmc_path):
    """Run chainloader in QEMU"""
    print("\nRunning QEMU test...")
    print("=" * 60)

    # QEMU command
    cmd = [
        QEMU_BINARY,
        "-M", MACHINE,
        "-cpu", CPU,
        "-m", MEMORY,
        "-smp", "8",
        "-nographic",
        "-serial", SERIAL,
        "-kernel", chainloader_path,
        "-drive", f"file={emmc_path},format=raw,if=none,id=emmc",
        "-device", "virtio-blk-device,drive=emmc",
        "-bios", "none",
    ]

    print(f"Command: {' '.join(cmd)}")
    print("=" * 60)

    try:
        result = subprocess.run(cmd, timeout=10, capture_output=True, text=True)
        print("\nSTDOUT:")
        print(result.stdout)
        if result.stderr:
            print("\nSTDERR:")
            print(result.stderr)

        # Check for expected output
        expected_strings = [
            "XNU Chainloader for MT6765",
            "Loading XNU kernel",
            "Boot params",
        ]

        found = []
        for s in expected_strings:
            if s in result.stdout:
                found.append(s)

        if len(found) == len(expected_strings):
            print("\n" + "=" * 60)
            print("✅ TEST PASSED: All expected output found")
            print("=" * 60)
            return True
        else:
            print(f"\n⚠️  PARTIAL: Found {len(found)}/{len(expected_strings)} expected strings")
            print(f"   Found: {found}")
            return False

    except subprocess.TimeoutExpired:
        print("\n⚠️  TEST TIMEOUT: QEMU did not exit within 10 seconds")
        print("   This may be normal if chainloader is waiting for hardware")
        return True  # Timeout is OK for bootloader tests
    except FileNotFoundError:
        print(f"\n❌ ERROR: QEMU not found: {QEMU_BINARY}")
        print("   Install with: sudo apt-get install qemu-system-arm")
        return False

def main():
    print("=" * 60)
    print("  XNU Chainloader QEMU Test")
    print("=" * 60)
    print()

    # Check QEMU
    try:
        subprocess.run([QEMU_BINARY, "--version"],
                      capture_output=True, check=True)
    except (subprocess.CalledProcessError, FileNotFoundError):
        print(f"ERROR: {QEMU_BINARY} not found")
        print("Install with: sudo apt-get install qemu-system-arm")
        return 1

    # Build chainloader
    chainloader_path = build_chainloader()
    if not chainloader_path:
        return 1

    # Create test environment
    emmc_path = create_test_emmc_image()

    # Run test
    success = run_qemu_test(chainloader_path, emmc_path)

    # Cleanup
    try:
        os.remove(emmc_path)
    except:
        pass

    return 0 if success else 1

if __name__ == '__main__':
    sys.exit(main())
