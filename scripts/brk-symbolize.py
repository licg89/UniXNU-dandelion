#!/usr/bin/env python3
"""Symbolize the DANDELION-BRK lines the slide build prints.

Feed it the decoded kernel console text.  For every `DANDELION-BRK: ... unslid=...` record it
prints the owning dyld-cache image and, from the local symbol table, the function name.

    python3 scripts/brk-symbolize.py console.txt \
        --cache-header /root/work/symtab/dyld_shared_cache_arm64 \
        --symbols      /root/work/symtab/dyld_shared_cache_arm64.symbols

`unslid = pc - slide` is the address in the shared-region image table, so that is the number to
query.  pc, slide and unslid are printed side by side: the image name has to match the process
that printed the line, otherwise the slide (or the pc) is wrong.

Layout facts are the ones scripts/local-symbols.py established from the files:
  cache header   imagesOffset/Count at 0x1C0/0x1C4, sharedRegionStart at 0xE0, 0x20-byte entries
  .symbols       0x4000: nlistOffset, nlistCount, stringsOffset, stringsSize, entriesOffset,
                 entriesCount; 16-byte local entries (dylibOffset u64, start u32, count u32);
                 16-byte nlist entries (n_strx u32, ..., n_value u64 at +8).
"""
import argparse
import re
import struct

LOCAL_SYMBOLS_BASE = 0x4000
ENTRY_SIZE = 16

BRK = re.compile(
    r'DANDELION-BRK: pid=(-?\d+) comm=(\S+) pc=(0x[0-9a-fA-F]+) slide=(0x[0-9a-fA-F]+)'
    r' unslid=(0x[0-9a-fA-F]+) lr=(0x[0-9a-fA-F]+) comment=(0x[0-9a-fA-F]+)')

# Newer images print the shared region's own slide alongside the per-task one.
SR = re.compile(
    r'DANDELION-SR: pid=(-?\d+) comm=(\S+) sr_slide=(0x[0-9a-fA-F]+) sr_base=(0x[0-9a-fA-F]+)'
    r' task_slide=(0x[0-9a-fA-F]+)')

UNKNOWN = (0xffffffffffffffff, 0xffffffff)


def read_images(path):
    with open(path, 'rb') as fh:
        head = fh.read(0x400)
        if not head.startswith(b'dyld_v1'):
            raise SystemExit('cache header file not recognised: ' + path)
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


class Symbols:
    def __init__(self, path, region_start):
        self.fh = open(path, 'rb')
        self.region_start = region_start
        self.fh.seek(LOCAL_SYMBOLS_BASE)
        (self.nlist_off, self.nlist_cnt, self.str_off, self.str_size,
         ent_off, ent_cnt) = struct.unpack('<6I', self.fh.read(0x18))
        self.fh.seek(LOCAL_SYMBOLS_BASE + ent_off)
        ents = self.fh.read(ent_cnt * ENTRY_SIZE)
        self.table = {}
        for i in range(ent_cnt):
            off = i * ENTRY_SIZE
            dylib_offset = struct.unpack_from('<Q', ents, off)[0]
            start, count = struct.unpack_from('<II', ents, off + 8)
            self.table[dylib_offset] = (start, count)

    def name_at(self, base, target):
        entry = self.table.get(base - self.region_start)
        if entry is None or entry[1] == 0:
            return None
        start, count = entry
        self.fh.seek(LOCAL_SYMBOLS_BASE + self.nlist_off + start * 16)
        nl = self.fh.read(count * 16)
        best = None
        for k in range(min(count, len(nl) // 16)):
            strx = struct.unpack_from('<I', nl, k * 16)[0]
            value = struct.unpack_from('<Q', nl, k * 16 + 8)[0]
            if value <= target and (best is None or value > best[0]):
                best = (value, strx)
        if best is None:
            return None
        self.fh.seek(LOCAL_SYMBOLS_BASE + self.str_off + best[1])
        sym = self.fh.read(256).split(b'\0')[0].decode('utf-8', 'replace')
        return '%s + 0x%x' % (sym, target - best[0])


def image_of(images, addr):
    found = None
    for address, name in images:
        if address <= addr:
            found = (address, name)
        else:
            break
    if found is None:
        return None, 0
    return found[1], addr - found[0]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('log')
    ap.add_argument('--cache-header', default='/root/work/symtab/dyld_shared_cache_arm64')
    ap.add_argument('--symbols', default='/root/work/symtab/dyld_shared_cache_arm64.symbols')
    ap.add_argument('--out', default='')
    args = ap.parse_args()

    text = open(args.log, 'rb').read().decode('latin-1', 'replace')
    hits = BRK.findall(text)
    if not hits:
        print('no DANDELION-BRK records with slide/unslid in', args.log)
        return 1

    # The kernel also prints the shared region's own slide (DANDELION-SR).  That one is always set
    # once the cache is mapped, while task_shared_region_slide stays -1 unless a copyin succeeded
    # during initial task setup, so the region value wins whenever the two disagree.
    sr_by_pid = {}
    for pid, comm, sr_slide, sr_base, task_slide in SR.findall(text):
        sr_by_pid[pid] = (int(sr_slide, 16), int(sr_base, 16))

    images, region_start = read_images(args.cache_header)
    try:
        syms = Symbols(args.symbols, region_start)
    except Exception as exc:                                    # noqa: BLE001
        print('symbol table unavailable:', exc)
        syms = None

    rows = []
    seen = set()
    for pid, comm, pc, slide, unslid, lr, comment in hits:
        if (comm, pc) in seen:
            continue
        seen.add((comm, pc))
        region_slide, region_base = sr_by_pid.get(pid, (None, None))
        chosen = int(slide, 16)
        source = 'task'
        if region_slide is not None and region_slide not in UNKNOWN:
            chosen = region_slide
            source = 'region'
        note = ''
        if chosen in UNKNOWN:
            note = '  (no usable slide was recorded for this task)'
        addr = (int(pc, 16) - chosen) & 0xffffffffffffffff
        row = ('%s  pid=%s  pc=%s  slide=%s  unslid=%s  lr=%s  comment=%s\n'
               '    slide source: %s%s'
               % (comm, pid, pc, slide, unslid, lr, comment, source, note))
        if region_base is not None:
            row += '  sr_base=%#x' % region_base
        if source == 'region' and chosen != int(slide, 16):
            row += '\n    note: the per-task value %s is not the region slide; using %#x' % (slide, chosen)
        name, offset = image_of(images, addr)
        if name is None:
            row += '\n    image: outside the shared region (the process own Mach-O text)'
        else:
            row += '\n    image: %s + 0x%x' % (name, offset)
            if syms:
                fn = syms.name_at(addr - offset, addr)
                row += '\n    symbol: %s' % (fn if fn else '(no local symbol at or below)')
        rows.append(row)

    body = '\n'.join(rows)
    print(body)
    if args.out:
        with open(args.out, 'w') as fh:
            fh.write(body + '\n')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
