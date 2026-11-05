#!/usr/bin/env python3
"""Write a multi-window A04RPOL1 policy region into the userdata image.

Each window needs its own signed envelope: the driver selects the entry whose signed
image_bytes equals the window's size, so OS and SystemOS both have to be present before the
SystemOS volume can be published (it carries the dyld shared cache launchd needs).

    write_policy_bundle.py --image userdata-signed.img --out-dir vr --os OS --systemos SystemOS
"""
import argparse
import hashlib
import pathlib
import struct
import sys

HEADER = 4096
ALIGN = 1 << 20

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--image', type=pathlib.Path, required=True)
parser.add_argument('--out-dir', type=pathlib.Path, required=True, help='directory holding <name>.envelope/manifest.bin')
parser.add_argument('--names', nargs='+', required=True, help='window names, in the order to write')
args = parser.parse_args()

with args.image.open('r+b') as stream:
    manifest = stream.read(HEADER)
    if manifest[:8] != b'SHNGROOT':
        sys.exit('image does not start with SHNGROOT')
    count = struct.unpack_from('<I', manifest, 12)[0]
    windows = {}
    end = 0
    for index in range(count):
        at = 24 + 128 * index
        name = manifest[at:at + 32].split(b'\0')[0].decode()
        offset, size = struct.unpack_from('<QQ', manifest, at + 32)
        windows[name] = (offset, size)
        print(f'window {name}: offset {offset:#x} size {size}')
        end = max(end, offset + size)

    start = (end + ALIGN - 1) & ~(ALIGN - 1)
    envelopes = []
    for name in args.names:
        if name not in windows:
            sys.exit(f'the manifest has no window named {name}')
        path = args.out_dir / f'{name}.envelope' / 'manifest.bin'
        if not path.is_file():
            sys.exit(f'missing envelope {path}')
        data = path.read_bytes()
        if not data.startswith(b'VROOT01'):
            sys.exit(f'{path} is not a VROOT01 envelope')
        envelopes.append((name, data))

    total = HEADER + sum(len(data) for _, data in envelopes)
    if total > 256 << 20:
        sys.exit('the region exceeds the 256 MiB the driver accepts')

    region = bytearray(total)
    struct.pack_into('<8sIIQ', region, 0, b'A04RPOL1', 1, len(envelopes), total)
    offset = HEADER
    for index, (name, data) in enumerate(envelopes):
        struct.pack_into('<QQ', region, 24 + index * 16, offset, len(data))
        region[offset:offset + len(data)] = data
        print(f'entry {index}: {name} envelope {len(data)} bytes at region +{offset:#x}')
        offset += len(data)
    region[4064:4096] = hashlib.sha256(bytes(region[:HEADER])).digest()

    stream.seek(start)
    stream.write(region)
    size = stream.seek(0, 2)
print(f'region {total} bytes at {start:#x}; image {size} bytes; header sha256 {region[4064:4096].hex()[:32]}...')
