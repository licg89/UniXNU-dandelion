#!/usr/bin/env python3
"""Symbolize unslid dyld-shared-cache addresses against the local cache and its .symbols table."""
import struct
import sys
import pathlib

CACHE = pathlib.Path('/root/work/symtab/dyld_shared_cache_arm64')
SYMBOLS = pathlib.Path('/root/work/symtab/dyld_shared_cache_arm64.symbols')
SYM_BASE = 0x4000


def main():
    slide = int(sys.argv[1], 16)
    targets = [int(a, 16) for a in sys.argv[2:]]

    header = CACHE.read_bytes()
    region_start = struct.unpack_from('<Q', header, 0xE0)[0]
    img_off, img_cnt = struct.unpack_from('<I', header, 0x1C0)[0], struct.unpack_from('<I', header, 0x1C4)[0]
    if len(sys.argv) > 2 and sys.argv[2] == '--info':
        print('region_start %#x, images %d' % (region_start, img_cnt))
        return

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

    def image_for(unslid):
        lo, hi, best = 0, len(images) - 1, None
        while lo <= hi:
            mid = (lo + hi) // 2
            if images[mid][0] <= unslid:
                best, lo = mid, mid + 1
            else:
                hi = mid - 1
        return images[best] if best is not None else None

    for target in targets:
        unslid = target - slide
        img = image_for(unslid)
        if img is None or unslid < region_start:
            print('0x%x -> unslid %#x: below the cache region (%#x)' % (target, unslid, region_start))
            continue
        base, name = img
        span = ranges.get(base - region_start)
        label = '0x%x -> unslid %#x  %s' % (target, unslid, name)
        if not span:
            print(label + '  (no local symbols)')
            continue
        start, count = span
        fh.seek(SYM_BASE + nlist_off + start * 16)
        blob = fh.read(count * 16)
        below_val, below_strx = None, None
        for k in range(count):
            strx = struct.unpack_from('<I', blob, k * 16)[0]
            value = struct.unpack_from('<Q', blob, k * 16 + 8)[0]
            if value <= unslid and (below_val is None or value > below_val):
                below_val, below_strx = value, strx
        if below_val is None:
            print(label + '  (no symbol at or below)')
            continue
        fh.seek(SYM_BASE + str_off + below_strx)
        sym = fh.read(192).split(b'\0')[0].decode('latin-1', 'replace')
        print('%s  ->  %s + 0x%x' % (label, sym, unslid - below_val))


if __name__ == '__main__':
    main()
