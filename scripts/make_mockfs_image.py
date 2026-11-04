#!/usr/bin/env python3
"""Assemble the mockfs test container the kernel mounts as root.

mockfs (bsd/miscfs/mockfs) does not read a filesystem at all: it treats the root
device as a header plus two blobs, exactly as mockfs_vfsops.c validates:

    magic "N4KROOT", version 1, count 2
    launch_offset == 4096, launch_size > 0          -> /sbin/launchd
    test_offset   4K aligned, >= 8192, + test_size <= device size -> /probe.bin

so the RAM disk is the device and mockfs is the filesystem.  This is the shape
the gta4l record calls a "RAM-backed mockfs root".

    make_mockfs_image.py launchd.macho probe.bin out.img
"""
import pathlib
import struct
import sys

launch = pathlib.Path(sys.argv[1]).read_bytes()
probe = pathlib.Path(sys.argv[2]).read_bytes() if len(sys.argv) > 3 else b'N4K probe\n'
target = pathlib.Path(sys.argv[3] if len(sys.argv) > 3 else sys.argv[2])

LAUNCH_OFFSET = 4096
test_offset = (LAUNCH_OFFSET + len(launch) + 4095) & ~4095
if test_offset < 8192:
    test_offset = 8192
total = (test_offset + len(probe) + 4095) & ~4095
# A little headroom keeps the device a sane size for the md driver.
total = max(total, 65536)

image = bytearray(total)
header = struct.pack('<8sIIQQQQ', b'N4KROOT', 1, 2,
                     LAUNCH_OFFSET, len(launch), test_offset, len(probe))
image[0:len(header)] = header
image[LAUNCH_OFFSET:LAUNCH_OFFSET + len(launch)] = launch
image[test_offset:test_offset + len(probe)] = probe
target.write_bytes(bytes(image))

print(f'{target}: {total} bytes, launchd {len(launch)} B at {LAUNCH_OFFSET:#x}, '
      f'probe {len(probe)} B at {test_offset:#x}')
print('  magic', header[:8], 'version', 1, 'count', 2)
