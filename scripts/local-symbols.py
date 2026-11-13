#!/usr/bin/env python3
"""Resolve cache addresses to image and symbol.

Record sizes were established from the files, not the documentation: this build uses the 64-bit local
symbol entry (dylibOffset u64, nlistStartIndex u32, nlistCount u32 -- 16 bytes), where the classic entry
is 12 bytes with a u32 offset.  Reading 12-byte records put every entry after the first out of step.

  0x4000 in the .symbols file:
      nlistOffset 0xd1c0, nlistCount 0x9bc3af
      stringsOffset 0x9bd0cb0, stringsSize 0x1f0ee453
      entriesOffset 0x18, entriesCount 0xd1a (3354, the image count)

The header file carries the image table at 0x270 (3354 x 0x20: address u64, modTime u64, inode u64,
pathFileOffset u32, pad u32) and the shared region start at 0xE0.
"""
import struct
import sys

LOCAL_SYMBOLS_BASE = 0x4000
ENTRY_SIZE = 16


def read_images(path):
    with open(path, 'rb') as fh:
        head = fh.read(0x400)
        if not head.startswith(b'dyld_v1'):
            raise SystemExit('cache header file not recognised')
        images_offset = struct.unpack_from('<I', head, 0x1C0)[0]
        images_count = struct.unpack_from('<I', head, 0x1C4)[0]
        region_start = struct.unpack_from('<Q', head, 0xE0)[0]
        fh.seek(images_offset)
        raw = fh.read(images_count * 0x20)
        images = []
        for i in range(images_count):
            b = i * 0x20
            address = struct.unpack_from('<Q', raw, b)[0]
            path_offset = struct.unpack_from('<I', raw, b + 0x18)[0]
            keep = fh.tell()
            fh.seek(path_offset)
            name = fh.read(256).split(b'\0')[0].decode('utf-8', 'replace')
            fh.seek(keep)
            images.append((address, name))
    images.sort()
    return images, region_start


def main():
    if len(sys.argv) < 4:
        raise SystemExit('usage: local-symbols.py <cache-header> <symbols> <addr> [addr ...]')

    images, region_start = read_images(sys.argv[1])
    print('images %d, shared region start %#x' % (len(images), region_start))

    with open(sys.argv[2], 'rb') as fh:
        fh.seek(LOCAL_SYMBOLS_BASE)
        nlist_off, nlist_cnt, str_off, str_size, ent_off, ent_cnt = struct.unpack('<6I', fh.read(0x18))
        print('local symbols: nlist %#x/%d strings %#x/%d entries %#x/%d' %
              (nlist_off, nlist_cnt, str_off, str_size, ent_off, ent_cnt))

        fh.seek(LOCAL_SYMBOLS_BASE + ent_off)
        ents = fh.read(ent_cnt * ENTRY_SIZE)
        table = {}
        good = 0
        for i in range(ent_cnt):
            off = i * ENTRY_SIZE
            dylib_offset = struct.unpack_from('<Q', ents, off)[0]
            start_index, count = struct.unpack_from('<II', ents, off + 8)
            if count:
                good += 1
            table[dylib_offset] = (start_index, count)
        print('entries with symbols: %d of %d' % (good, ent_cnt))

        for a in sys.argv[3:]:
            t = int(a, 16)
            img = None
            for address, name in images:
                if address <= t:
                    img = (address, name)
                else:
                    break
            if img is None:
                print('%#x -> outside every image' % t)
                continue
            base, iname = img
            entry = table.get(base - region_start)
            if entry is None:
                print('%#x -> %s + %#x  (no local-symbol entry)' % (t, iname, t - base))
                continue
            start_index, count = entry
            if count == 0:
                print('%#x -> %s + %#x  (entry has no symbols)' % (t, iname, t - base))
                continue

            fh.seek(LOCAL_SYMBOLS_BASE + nlist_off + start_index * 16)
            nl = fh.read(count * 16)
            best = None
            for k in range(min(count, len(nl) // 16)):
                n_strx = struct.unpack_from('<I', nl, k * 16)[0]
                n_value = struct.unpack_from('<Q', nl, k * 16 + 8)[0]
                if n_value <= t and (best is None or n_value > best[0]):
                    best = (n_value, n_strx)
            if best is None:
                print('%#x -> %s + %#x  (no symbol at or below)' % (t, iname, t - base))
                continue
            fh.seek(LOCAL_SYMBOLS_BASE + str_off + best[1])
            sym = fh.read(256).split(b'\0')[0].decode('utf-8', 'replace')
            print('%#x -> %s + %#x' % (t, iname, t - base))
            print('        = %s + %#x' % (sym, t - best[0]))


if __name__ == '__main__':
    main()
