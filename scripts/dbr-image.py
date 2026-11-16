#!/usr/bin/env python3
"""Unpack or repack a DBRDISK1 RAM disk image.

The seed for the Data volume is a sparse APFS container carried in DarwinBoot's DBRDISK1 format: a
32-byte header ('DBRDISK1', version, page size, logical size, extent count), one (offset, length) pair
per non-zero extent, then the extent payloads.  Everything outside an extent is zero, so a 256 MiB
container packs into about a megabyte.

Growing the container needs macOS (`hdiutil` and `diskutil apfs resizeContainer`), which is what the
resize-data-seed workflow uses this tool for: unpack, let macOS create the larger image, repack.
"""
import argparse
import struct
from pathlib import Path

PAGE = 4096
HEADER = struct.Struct('<8sIIQQ')
EXTENT = struct.Struct('<QQ')


def unpack(blob: bytes) -> bytes:
    if len(blob) < HEADER.size:
        raise ValueError('image is shorter than the DBRDISK1 header')
    magic, version, page, logical, count = HEADER.unpack_from(blob)
    if magic != b'DBRDISK1' or version != 1 or page != PAGE:
        raise ValueError(f'not a DBRDISK1 image: magic={magic!r} version={version} page={page}')
    if logical % PAGE:
        raise ValueError('logical size is not a multiple of the page size')
    cursor = HEADER.size + EXTENT.size * count
    image = bytearray(logical)
    for index in range(count):
        offset, length = EXTENT.unpack_from(blob, HEADER.size + EXTENT.size * index)
        if offset + length > logical or not length:
            raise ValueError(f'extent {index} is out of bounds')
        image[offset:offset + length] = blob[cursor:cursor + length]
        cursor += length
    if cursor != len(blob):
        raise ValueError('trailing bytes after the last extent')
    return bytes(image)


def pack(image: bytes) -> tuple[bytes, int]:
    if len(image) % PAGE:
        raise ValueError('image size is not a multiple of the page size')
    zero = bytes(PAGE)
    extents = []
    index, total = 0, len(image) // PAGE
    while index < total:
        if image[index * PAGE:(index + 1) * PAGE] == zero:
            index += 1
            continue
        start = index
        while index < total and image[index * PAGE:(index + 1) * PAGE] != zero:
            index += 1
        extents.append((start * PAGE, (index - start) * PAGE))
    header = HEADER.pack(b'DBRDISK1', 1, PAGE, len(image), len(extents))
    payload = b''.join(image[offset:offset + length] for offset, length in extents)
    return header + b''.join(EXTENT.pack(o, l) for o, l in extents) + payload, len(extents)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    action = parser.add_subparsers(dest='action', required=True)
    un = action.add_parser('unpack', help='write the raw container image')
    un.add_argument('packed', type=Path)
    un.add_argument('image', type=Path)
    re = action.add_parser('pack', help='write the packed RAM disk')
    re.add_argument('image', type=Path)
    re.add_argument('packed', type=Path)
    args = parser.parse_args()

    if args.action == 'unpack':
        image = unpack(args.packed.read_bytes())
        args.image.write_bytes(image)
        print(f'unpacked {args.packed.name}: {len(image)} bytes ({len(image) / 1048576:.0f} MiB)')
    else:
        packed, extents = pack(args.image.read_bytes())
        args.packed.write_bytes(packed)
        print(f'packed {args.image.name}: {len(packed)} bytes in {extents} extents')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
