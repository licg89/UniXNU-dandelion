#!/usr/bin/env python3
"""Name the shared-cache image containing each address, from `dyld_info -segments` output.

dyld_info prints one block per image:

    /usr/lib/libSystem.B.dylib:
        __TEXT  0x1800c0000  0x1234  ...

so the image base is the __TEXT vmaddr of each block.  Reading its output avoids parsing the cache header
by hand, and dyld_info is the one Apple tool on the runner that accepts these caches at all: atos refuses
them, nm does not recognise them, and the .symbols file is a binary table.
"""
import re
import sys

HEADER = re.compile(r'^(\S.*):$')
SEGMENT = re.compile(r'^\s+(\S+)\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)')


def main():
    if len(sys.argv) < 3:
        raise SystemExit('usage: image-from-segments.py <segments.txt> <addr> [addr ...]')

    targets = [int(a, 16) for a in sys.argv[2:]]
    images = []
    current = None
    with open(sys.argv[1], 'r', errors='replace') as fh:
        for line in fh:
            m = HEADER.match(line.rstrip('\n'))
            if m and not line.startswith('\t') and not line.startswith(' '):
                current = m.group(1)
                continue
            m = SEGMENT.match(line)
            if m and current and m.group(1) == '__TEXT':
                images.append((int(m.group(2), 16), current))
                current = None

    images.sort()
    print('images with __TEXT: %d' % len(images))
    if images:
        print('first: %#x %s' % images[0])
        print('last : %#x %s' % images[-1])
    print()
    for t in targets:
        best = None
        for idx, (base, name) in enumerate(images):
            if base <= t:
                best = idx
            else:
                break
        if best is None:
            print('0x%x -> below the first image' % t)
            continue
        base, name = images[best]
        nxt = images[best + 1][0] if best + 1 < len(images) else None
        span = ('next image %#x' % nxt) if nxt else 'last image'
        print('0x%x -> %s + 0x%x   (base %#x, %s)' % (t, name, t - base, base, span))


if __name__ == '__main__':
    main()
