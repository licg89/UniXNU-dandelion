#!/usr/bin/env python3
"""Lay the owner-signed policy region into the userdata image.

The driver finds it at policyRegionOffset(): the farthest window end rounded up to
kPolicyAlign, read from the image's own SHNGROOT manifest -- so this script derives
the position from the manifest rather than assuming it.  Layout per NativeMsdc.h:

    0      "A04RPOL1"
    8      u32 version = 1
    12     u32 count
    16     u64 total_bytes (header included)
    24     count x { u64 offset, u64 bytes }
    4064   SHA-256 of the 4096-byte header with these 32 bytes zeroed

    write_policy_region.py <userdata.img> <manifest.bin> [--out <copy>]
"""
import argparse
import hashlib
import pathlib
import shutil
import struct
import sys

POLICY_ALIGN = 1 << 20
HEADER_BYTES = 4096
MAGIC = b'A04RPOL1'

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('image', type=pathlib.Path)
parser.add_argument('envelope', type=pathlib.Path)
parser.add_argument('--out', type=pathlib.Path)
args = parser.parse_args()

image = args.image
target = args.out or image
if target != image:
    print(f'copying {image} -> {target}')
    shutil.copyfile(image, target)

with target.open('r+b') as stream:
    header = stream.read(HEADER_BYTES)
    if header[:8] != b'SHNGROOT':
        sys.exit('the image does not start with a SHNGROOT manifest')
    version, count = struct.unpack_from('<II', header, 8)
    print(f'manifest version {version}, {count} windows')
    end = 0
    # Each entry is 128 bytes: name[32] @0, offset @32, size @40, block size @48,
    # flags @52, sha256 @56 (NativeMsdc.h parseManifest).
    for index in range(count):
        at = 24 + 128 * index
        name = header[at:at + 32].split(b'\0')[0].decode('latin-1')
        offset, size = struct.unpack_from('<QQ', header, at + 32)
        print(f'  window {index} {name!r}: offset {offset:#x} size {size:#x}')
        end = max(end, offset + size)
    start = (end + POLICY_ALIGN - 1) & ~(POLICY_ALIGN - 1)
    envelope = args.envelope.read_bytes()
    if not envelope.startswith(b'VROOT01'):
        sys.exit(f'envelope magic is {envelope[:8]!r}, expected VROOT01')
    total = HEADER_BYTES + len(envelope)
    if total > 256 << 20:
        sys.exit('policy region exceeds the 256 MiB the driver accepts')
    print(f'region at {start:#x}, {total} bytes (envelope {len(envelope)})')

    region = bytearray(total)
    struct.pack_into('<8sIIQ', region, 0, MAGIC, 1, 1, total)
    struct.pack_into('<QQ', region, 24, HEADER_BYTES, len(envelope))
    digest = hashlib.sha256(bytes(region[:HEADER_BYTES])).digest()
    region[4064:4096] = digest
    region[HEADER_BYTES:] = envelope

    stream.seek(start)
    stream.write(region)
    stream.truncate(max(stream.seek(0, 2), start + total))
print(f'{target}: policy region written at {start:#x}, header sha256 {digest.hex()[:32]}...')
