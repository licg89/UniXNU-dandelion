#!/usr/bin/env python3
"""Map addresses to dyld shared cache images by reading the cache header's image table.

`dyld_cache_header` starts with magic[16], then mappingOffset/mappingCount (uint32) and
imagesOffset/imagesCount (uint32).  Each `dyld_cache_image_info` is 0x20 bytes: address, modTime, inode,
pathFileOffset, pad -- and the path is a NUL-terminated string at pathFileOffset in the same file.

That names the image for any address using only the cache header and the path strings, which is what is
needed here: atos refuses these caches, nm does not recognise them, and the .symbols file is a binary
table rather than text.
"""
import struct
import sys


def main():
    if len(sys.argv) < 3:
        raise SystemExit('usage: cache-images.py <cache-file> <addr> [addr ...]')

    path = sys.argv[1]
    targets = [int(a, 16) for a in sys.argv[2:]]

    with open(path, 'rb') as fh:
        header = fh.read(0x100)
        if not header.startswith(b'dyld_v1'):
            raise SystemExit('not a dyld shared cache: %r' % header[:16])

        mapping_offset, mapping_count, images_offset, images_count = struct.unpack_from('<IIII', header, 0x10)
        print('mappings %d at %#x; images %d at %#x' % (mapping_count, mapping_offset, images_count, images_offset))
        if not (0 < images_count < 100000):
            raise SystemExit('implausible image count: %d' % images_count)

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
        print('first image: %#x %s' % images[0])
        print('last  image: %#x %s' % images[-1])
        print()

        for t in targets:
            hit = None
            for idx, (address, name) in enumerate(images):
                if address <= t:
                    hit = (idx, address, name)
                else:
                    break
            if hit is None:
                print('0x%x -> below the first image' % t)
                continue
            idx, address, name = hit
            nxt = images[idx + 1][0] if idx + 1 < len(images) else None
            span = ('end %#x' % nxt) if nxt else 'last image'
            print('0x%x -> %s + 0x%x   (image starts %#x, %s)' % (t, name, t - address, address, span))


if __name__ == '__main__':
    main()
