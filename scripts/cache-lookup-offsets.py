#!/usr/bin/env python3
"""Report the image and offset-from-base for unslid cache addresses, plus the nearest symbols both ways."""
import struct
import sys
import pathlib

CACHE = pathlib.Path('/root/work/symtab/dyld_shared_cache_arm64')
SYMBOLS = pathlib.Path('/root/work/symtab/dyld_shared_cache_arm64.symbols')
SYM_BASE = 0x4000


def load():
    header = CACHE.read_bytes()
    region_start = struct.unpack_from('<Q', header, 0xE0)[0]
    img_off, img_cnt = struct.unpack_from('<I', header, 0x1C0)[0], struct.unpack_from('<I', header, 0x1C4)[0]
    images = []
    for i in range(img_cnt):
        b = img_off + i * 0x20
        addr = struct.unpack_from('<Q', header, b)[0]
        path_off = struct.unpack_from('<I', header, b + 0x18)[0]
        name = header[path_off:path_off + 300].split(b'\0')[0].decode('utf-8', 'replace')
        images.append((addr, name))
    images.sort()

    fh = open(SYMBOLS, 'rb')
    fh.seek(SYM_BASE)
    nlist_off, nlist_cnt, str_off, str_size, ent_off, ent_cnt = struct.unpack('<6I', fh.read(0x18))
    fh.seek(SYM_BASE + ent_off)
    ents = fh.read(ent_cnt * 16)
    ranges = {}
    for i in range(ent_cnt):
        off = i * 16
        dylib_offset = struct.unpack_from('<Q', ents, off)[0]
        start, count = struct.unpack_from('<II', ents, off + 8)
        if count:
            ranges[dylib_offset] = (start, count)
    return region_start, images, ranges, fh, nlist_off, str_off


def main():
    slide = int(sys.argv[1], 16)
    targets = [int(a, 16) for a in sys.argv[2:]]
    region_start, images, ranges, fh, nlist_off, str_off = load()

    def image_for(unslid):
        lo, hi, best = 0, len(images) - 1, None
        while lo <= hi:
            mid = (lo + hi) // 2
            if images[mid][0] <= unslid:
                best, lo = mid, mid + 1
            else:
                hi = mid - 1
        return images[best] if best is not None else None

    def syms_for(base):
        span = ranges.get(base - region_start)
        if not span:
            return []
        start, count = span
        fh.seek(SYM_BASE + nlist_off + start * 16)
        blob = fh.read(count * 16)
        out = []
        for k in range(count):
            strx = struct.unpack_from('<I', blob, k * 16)[0]
            value = struct.unpack_from('<Q', blob, k * 16 + 8)[0]
            pos = fh.tell()
            fh.seek(SYM_BASE + str_off + strx)
            name = fh.read(192).split(b'\0')[0].decode('latin-1', 'replace')
            fh.seek(pos)
            out.append((value, name))
        out.sort()
        return out

    cache = {}
    for target in targets:
        unslid = target - slide
        img = image_for(unslid)
        if img is None:
            print('0x%x -> unslid %#x: no image' % (target, unslid)); continue
        base, name = img
        if base not in cache:
            cache[base] = syms_for(base)
        syms = cache[base]
        offset = unslid - base
        below = None
        above = None
        for value, sym in syms:
            if value <= unslid:
                below = (value, sym)
            elif above is None:
                above = (value, sym)
        print('0x%x -> %s + %#x' % (target, name.rsplit('/', 1)[-1], offset))
        if below:
            print('      below: %s + %#x' % (below[1], unslid - below[0]))
        if above:
            print('      above: %s - %#x' % (above[1], above[0] - unslid))


if __name__ == '__main__':
    main()
