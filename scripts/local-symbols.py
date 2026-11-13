#!/usr/bin/env python3
"""Read dyld's local symbol table and resolve cache addresses to image + symbol.

Inputs are the two files that sit next to each other in the cache directory:

  dyld_shared_cache_arm64           header, image table (3354 entries here), path strings
  dyld_shared_cache_arm64.symbols   dyld_cache_local_symbols_info + nlist + strings

Both are binary.  Layouts, from dyld's sources:

  struct dyld_cache_local_symbols_info {
      uint32_t nlistOffset, nlistCount;      // 0, 4
      uint32_t stringsOffset, stringsSize;   // 8, 12
      uint32_t entriesOffset, entriesCount;  // 16, 20
  };

  struct dyld_cache_local_symbols_entry {    // 12 bytes
      uint32_t dylibOffset;                  // VM offset of the image from the cache base
      uint32_t nlistStartIndex, nlistCount;
  };

Each nlist64 in the nlist array is { uint32 n_strx; uint8 n_type, n_sect; uint16 n_desc; uint64 n_value; }
-- 16 bytes on 64-bit caches -- and n_value is the symbol address in the shared region.
"""
import struct
import sys

IMAGES_OFFSET_FIELD = 0x1C0
IMAGES_COUNT_FIELD = 0x1C4
SHARED_REGION_START_FIELD = 0xE0


def read_images(path):
    with open(path, 'rb') as fh:
        head = fh.read(0x400)
        if not head.startswith(b'dyld_v1'):
            raise SystemExit('cache header file not recognised')
        images_offset = struct.unpack_from('<I', head, IMAGES_OFFSET_FIELD)[0]
        images_count = struct.unpack_from('<I', head, IMAGES_COUNT_FIELD)[0]
        region_start = struct.unpack_from('<Q', head, SHARED_REGION_START_FIELD)[0]
        print('images %d at %#x, shared region start %#x' % (images_count, images_offset, region_start))
        fh.seek(images_offset)
        raw = fh.read(images_count * 0x20)
        images = []
        for i in range(images_count):
            base = i * 0x20
            address = struct.unpack_from('<Q', raw, base)[0]
            path_offset = struct.unpack_from('<I', raw, base + 0x18)[0]
            where = fh.tell()
            fh.seek(path_offset)
            name = fh.read(256).split(b'\0')[0].decode('utf-8', 'replace')
            fh.seek(where)
            images.append((address, name))
    images.sort()
    return images, region_start


def read_local_symbols(path):
    with open(path, 'rb') as fh:
        head = fh.read(0x40)
        magic = head[:16]
        print('symbols magic: %r' % magic)
        # The .symbols file starts with the cache magic too; the info struct follows it.
        off = 0x18 if head.startswith(b'dyld_v1') else 0x0
        nlist_offset, nlist_count, strings_offset, strings_size, entries_offset, entries_count = \
            struct.unpack_from('<IIIIII', head, off)
        print('nlist %d at %#x, strings %d at %#x, entries %d at %#x' %
              (nlist_count, nlist_offset, strings_size, strings_offset, entries_count, entries_offset))
        if not (0 < entries_count < 100000 and 0 < nlist_count < 5000000):
            raise SystemExit('implausible counts; layout may differ for this build')

        fh.seek(entries_offset)
        ents = fh.read(entries_count * 12)
        entries = []
        for i in range(entries_count):
            base = i * 12
            dylib_offset, start_index, count = struct.unpack_from('<III', ents, base)
            entries.append((dylib_offset, start_index, count))

        fh.seek(nlist_offset)
        nlists = fh.read(nlist_count * 16)
        fh.seek(strings_offset)
        strings = fh.read(strings_size)

    return entries, nlists, strings, nlist_count


def symbol_name(strings, n_strx):
    if n_strx >= len(strings):
        return '?'
    end = strings.find(b'\0', n_strx)
    return strings[n_strx:end].decode('utf-8', 'replace')


def main():
    if len(sys.argv) < 4:
        raise SystemExit('usage: local-symbols.py <cache-header> <symbols-file> <addr> [addr ...]')

    images, region_start = read_images(sys.argv[1])
    entries, nlists, strings, nlist_count = read_local_symbols(sys.argv[2])
    targets = [int(a, 16) for a in sys.argv[3:]]

    for t in targets:
        # image for the address
        img = None
        for idx, (address, name) in enumerate(images):
            if address <= t:
                img = (idx, address, name)
            else:
                break
        if img is None:
            print('%#x -> outside every image' % t)
            continue
        idx, base, iname = img
        img_off = base - region_start

        # local symbol entry for that image
        entry = None
        for dylib_offset, start_index, count in entries:
            if dylib_offset == img_off:
                entry = (start_index, count)
                break
        if entry is None:
            print('%#x -> %s + %#x  (no local symbol entry for this image)' % (t, iname, t - base))
            continue

        start_index, count = entry
        best = None
        for k in range(start_index, min(start_index + count, nlist_count)):
            base_off = k * 16
            n_strx = struct.unpack_from('<I', nlists, base_off)[0]
            n_value = struct.unpack_from('<Q', nlists, base_off + 8)[0]
            if n_value <= t:
                if best is None or n_value > best[0]:
                    best = (n_value, n_strx)
        if best is None:
            print('%#x -> %s + %#x  (no symbol at or below)' % (t, iname, t - base))
        else:
            print('%#x -> %s + %#x   [%s + %#x]' %
                  (t, iname, t - base, symbol_name(strings, best[1]), t - best[0]))


if __name__ == '__main__':
    main()
