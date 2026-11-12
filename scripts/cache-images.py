#!/usr/bin/env python3
"""Name the shared-cache image for each address, from the cache header file alone.

The cache's main file is small (the bulk lives in subcaches) but it carries the header, the image table
and the path strings.  For this iOS 17.7.11 arm64 cache the table fields are at 0x1C0/0x1C4 rather than
the classic 0x18/0x1C, which is why reading the classic offsets found imagesCount 0:

    0x01C0  u32 imagesOffset = 0x270
    0x01C4  u32 imagesCount  = 0xd1a (3354)

Each entry is dyld_cache_image_info: address (u64), modTime (u64), inode (u64), pathFileOffset (u32),
pad (u32).  The path is a NUL-terminated string at pathFileOffset in the same file.
"""
import struct
import sys

IMAGES_OFFSET_FIELD = 0x1C0
IMAGES_COUNT_FIELD = 0x1C4
CLASSIC_OFFSET_FIELD = 0x18
CLASSIC_COUNT_FIELD = 0x1C


def read_u32(buf, off):
    return struct.unpack_from('<I', buf, off)[0]


def load_images(path):
    with open(path, 'rb') as fh:
        head = fh.read(0x400)
        if not head.startswith(b'dyld_v1'):
            raise SystemExit('not a dyld shared cache header file')
        images_offset = read_u32(head, IMAGES_OFFSET_FIELD)
        images_count = read_u32(head, IMAGES_COUNT_FIELD)
        if not (0 < images_offset < len(head) * 0x1000 and 0 < images_count < 200000):
            images_offset = read_u32(head, CLASSIC_OFFSET_FIELD)
            images_count = read_u32(head, CLASSIC_COUNT_FIELD)
            print('fell back to the classic offsets: %d images at %#x' % (images_count, images_offset))
        else:
            print('image table: %d entries at %#x' % (images_count, images_offset))

        fh.seek(images_offset)
        raw = fh.read(images_count * 0x20)
        images = []
        for i in range(images_count):
            base = i * 0x20
            address, mod_time, inode, path_offset, pad = struct.unpack_from('<QQQII', raw, base)
            fh.seek(path_offset)
            name = fh.read(256).split(b'\0')[0].decode('utf-8', 'replace')
            images.append((address, name))
    images.sort()
    return images


def main():
    if len(sys.argv) < 3:
        raise SystemExit('usage: cache-images.py <cache-header-file> <addr> [addr ...]')
    images = load_images(sys.argv[1])
    targets = [int(a, 16) for a in sys.argv[2:]]
    print('images: %d   first %#x   last %#x' % (len(images), images[0][0], images[-1][0]))
    for t in targets:
        best = None
        for idx, (address, name) in enumerate(images):
            if address <= t:
                best = idx
            else:
                break
        if best is None:
            print('%#x -> below the first image' % t)
            continue
        address, name = images[best]
        nxt = images[best + 1][0] if best + 1 < len(images) else address
        print('%#x -> %s + %#x   (image base %#x, next %#x)' % (t, name, t - address, address, nxt))


if __name__ == '__main__':
    main()
