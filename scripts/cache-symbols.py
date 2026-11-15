#!/usr/bin/env python3
"""Dump symbols of one cache image whose names match a pattern, with their unslid addresses."""
import argparse
import pathlib
import struct

HEADER = pathlib.Path('/root/work/symtab/dyld_shared_cache_arm64').read_bytes()
SYMBOLS = pathlib.Path('/root/work/symtab/dyld_shared_cache_arm64.symbols')
SYM_BASE = 0x4000
REGION_START = struct.unpack_from('<Q', HEADER, 0xE0)[0]

img_off, img_cnt = struct.unpack_from('<I', HEADER, 0x1C0)[0], struct.unpack_from('<I', HEADER, 0x1C4)[0]
images = []
for i in range(img_cnt):
    b = img_off + i * 0x20
    addr = struct.unpack_from('<Q', HEADER, b)[0]
    path_off = struct.unpack_from('<I', HEADER, b + 0x18)[0]
    name = HEADER[path_off:path_off + 300].split(b'\0')[0].decode('utf-8', 'replace')
    images.append((addr, name))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('image')
    ap.add_argument('pattern')
    ap.add_argument('--limit', type=int, default=60)
    args = ap.parse_args()

    hit = [(a, n) for a, n in images if args.image in n]
    if not hit:
        raise SystemExit('no image matching %r' % args.image)
    base, name = hit[0]
    print('image: %s  unslid base %#x' % (name, base))

    fh = open(SYMBOLS, 'rb')
    fh.seek(SYM_BASE)
    nlist_off, nlist_cnt, str_off, str_size, ent_off, ent_cnt = struct.unpack('<6I', fh.read(0x18))
    fh.seek(SYM_BASE + ent_off)
    ents = fh.read(ent_cnt * 16)
    entry = None
    for i in range(ent_cnt):
        off = i * 16
        dylib_offset = struct.unpack_from('<Q', ents, off)[0]
        start, count = struct.unpack_from('<II', ents, off + 8)
        if base - REGION_START == dylib_offset:
            entry = (start, count)
            break
    if not entry or entry[1] == 0:
        raise SystemExit('no local symbols for this image')
    start, count = entry
    print('local symbols: %d' % count)
    fh.seek(SYM_BASE + nlist_off + start * 16)
    nl = fh.read(count * 16)
    found = 0
    for k in range(count):
        strx = struct.unpack_from('<I', nl, k * 16)[0]
        value = struct.unpack_from('<Q', nl, k * 16 + 8)[0]
        fh.seek(SYM_BASE + str_off + strx)
        sym = fh.read(192).split(b'\0')[0].decode('latin-1', 'replace')
        if args.pattern in sym:
            print('  %#x  %s' % (value, sym))
            found += 1
            if found >= args.limit:
                break
    print('matches: %d' % found)


if __name__ == '__main__':
    main()
