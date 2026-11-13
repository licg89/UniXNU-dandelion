#!/usr/bin/env python3
"""Resolve trap addresses when each process slides the shared cache differently.

The shared cache is slid per process, so the same trap appears at a different absolute address on every
boot (measured: 0x1c39224f8 in one run, 0x1e15624f8 in the next, with identical low bits).  Slides are
multiples of 16 MB, so the slide can be recovered: for each candidate, the address must fall inside one of
the cache's images at an offset the image actually contains.

Once the image and offset are known the local symbol table gives the function.
"""
import struct
import sys

IMAGES_OFFSET_FIELD = 0x1C0
IMAGES_COUNT_FIELD = 0x1C4
REGION_START_FIELD = 0xE0
REGION_SIZE_FIELD = 0xE8
SLIDE_STEP = 0x1000000  # 16 MB
SLIDE_SEARCH = 0x40000000  # up to 1 GB of slide


def read_images(path):
    with open(path, 'rb') as fh:
        head = fh.read(0x400)
        images_offset = struct.unpack_from('<I', head, IMAGES_OFFSET_FIELD)[0]
        images_count = struct.unpack_from('<I', head, IMAGES_COUNT_FIELD)[0]
        region_start = struct.unpack_from('<Q', head, REGION_START_FIELD)[0]
        region_size = struct.unpack_from('<Q', head, REGION_SIZE_FIELD)[0]
        fh.seek(images_offset)
        raw = fh.read(images_count * 0x20)
        images = []
        for i in range(images_count):
            b = i * 0x20
            address = struct.unpack_from('<Q', raw, b)[0]
            path_offset = struct.unpack_from('<I', raw, b + 0x18)[0]
            keep = fh.tell()
            fh.seek(path_offset)
            name = fh.read(192).split(b'\0')[0].decode('utf-8', 'replace')
            fh.seek(keep)
            images.append((address, name))
        # sizes from the gap to the next image
        images.sort()
        sized = []
        for i, (address, name) in enumerate(images):
            nxt = images[i + 1][0] if i + 1 < len(images) else region_start + region_size
            sized.append((address, nxt, name))
    return sized, region_start


def find_position(images, region_start, pc):
    """Return (image_name, offset) for the slide that puts pc inside an image."""
    hits = []
    for slide in range(0, SLIDE_SEARCH + 1, SLIDE_STEP):
        base_shifted = pc - slide
        if base_shifted < region_start:
            continue
        for address, nxt, name in images:
            if address <= base_shifted < nxt:
                hits.append((slide, name, base_shifted - address, nxt - address))
                break
    return hits


def main():
    if len(sys.argv) < 4:
        raise SystemExit('usage: resolve-slid.py <cache-header> <symbols> <addr> [addr ...]')
    images, region_start = read_images(sys.argv[1])
    print('images %d, region start %#x' % (len(images), region_start))

    sym_path = sys.argv[2]
    with open(sym_path, 'rb') as fh:
        fh.seek(0x4000)
        nlist_off, nlist_cnt, str_off, str_size, ent_off, ent_cnt = struct.unpack('<6I', fh.read(0x18))
        fh.seek(0x4000 + ent_off)
        ents = fh.read(ent_cnt * 16)
        table = {}
        for i in range(ent_cnt):
            o = i * 16
            dylib_offset = struct.unpack_from('<Q', ents, o)[0]
            start_index, count = struct.unpack_from('<II', ents, o + 8)
            table[dylib_offset] = (start_index, count)
        print('symbol entries %d' % ent_cnt)

        for a in sys.argv[3:]:
            pc = int(a, 16)
            hits = find_position(images, region_start, pc)
            print()
            print('%#x: %d slide candidate(s)' % (pc, len(hits)))
            for slide, name, offset, size in hits[:4]:
                entry = table.get(int(name.split()[0], 16) if False else None)
                print('   slide %#x -> %s + %#x  (image size %#x)' % (slide, name, offset, size))
                # resolve the symbol: image vmaddr is pc - slide - offset
                img_vmaddr = pc - slide - offset
                entry = table.get(img_vmaddr - region_start)
                if entry is None:
                    print('        (no local-symbol entry for %#x)' % (img_vmaddr - region_start))
                    continue
                start_index, count = entry
                if count == 0:
                    print('        (entry has no symbols)')
                    continue
                fh.seek(0x4000 + nlist_off + start_index * 16)
                nl = fh.read(count * 16)
                best = None
                for k in range(min(count, len(nl) // 16)):
                    n_strx = struct.unpack_from('<I', nl, k * 16)[0]
                    n_value = struct.unpack_from('<Q', nl, k * 16 + 8)[0]
                    if n_value <= pc - slide and (best is None or n_value > best[0]):
                        best = (n_value, n_strx)
                if best is None:
                    print('        (no symbol at or below)')
                    continue
                fh.seek(0x4000 + str_off + best[1])
                sym = fh.read(192).split(b'\0')[0].decode('utf-8', 'replace')
                print('        = %s + %#x' % (sym, (pc - slide) - best[0]))


if __name__ == '__main__':
    main()
