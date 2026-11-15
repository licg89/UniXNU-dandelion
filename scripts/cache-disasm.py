#!/usr/bin/env python3
"""Disassemble one function from a cache image and name every call it makes.

    python3 scripts/cache-disasm.py QuartzCore.framework/QuartzCore '+[CADisplay mainDisplay]' --len 0x120
"""
import argparse
import pathlib
import re
import struct
import subprocess

HEADER = pathlib.Path('/root/work/symtab/dyld_shared_cache_arm64').read_bytes()
SYMBOLS = pathlib.Path('/root/work/symtab/dyld_shared_cache_arm64.symbols')
IMG = pathlib.Path('/root/work/UniXNU/out/dandelion/ipados-17.7.11-21H461/dandelion/userdata/userdata-root.img')
SYM_BASE = 0x4000
REGION = 0x180000000
SIZE = struct.unpack_from('<Q', HEADER, 0xE8)[0]
REGION_START = struct.unpack_from('<Q', HEADER, 0xE0)[0]
MAIN_FILE = 0x16bda6000


def load_tables():
    img_off, img_cnt = struct.unpack_from('<I', HEADER, 0x1C0)[0], struct.unpack_from('<I', HEADER, 0x1C4)[0]
    images = []
    for i in range(img_cnt):
        b = img_off + i * 0x20
        addr = struct.unpack_from('<Q', HEADER, b)[0]
        path_off = struct.unpack_from('<I', HEADER, b + 0x18)[0]
        name = HEADER[path_off:path_off + 300].split(b'\0')[0].decode('utf-8', 'replace')
        images.append((addr, name))
    images.sort()
    shards = []
    for line in pathlib.Path('/root/work/vr/shardmap.txt').read_text().splitlines():
        _i, f, v = line.split()
        shards.append((int(v, 16), int(f, 16)))
    shards.sort()
    windows = [(0, MAIN_FILE, shards[0][0] if shards else SIZE)]
    for k, (v, f) in enumerate(shards):
        windows.append((v, f, shards[k + 1][0] if k + 1 < len(shards) else SIZE))
    return images, windows


IMAGES, WINDOWS = load_tables()


def read(region_off, length):
    for lo, f, hi in WINDOWS:
        if lo <= region_off < hi:
            with open(IMG, 'rb') as fh:
                fh.seek(f + (region_off - lo))
                return fh.read(min(length, hi - region_off))
    return b''


class Sym:
    def __init__(self):
        fh = open(SYMBOLS, 'rb')
        fh.seek(SYM_BASE)
        (self.nlist_off, self.nlist_cnt, self.str_off, self.str_size,
         ent_off, ent_cnt) = struct.unpack('<6I', fh.read(0x18))
        fh.seek(SYM_BASE + ent_off)
        ents = fh.read(ent_cnt * 16)
        self.table = {}
        for i in range(ent_cnt):
            off = i * 16
            base = struct.unpack_from('<Q', ents, off)[0]
            start, count = struct.unpack_from('<II', ents, off + 8)
            self.table[base] = (start, count)
        self.fh = fh

    def name_at(self, absolute):
        base, _name = image_of(absolute)
        if base is None:
            return None
        entry = self.table.get(base - REGION_START)
        if not entry or entry[1] == 0:
            return None
        start, count = entry
        self.fh.seek(SYM_BASE + self.nlist_off + start * 16)
        nl = self.fh.read(count * 16)
        best = None
        for k in range(min(count, len(nl) // 16)):
            strx = struct.unpack_from('<I', nl, k * 16)[0]
            value = struct.unpack_from('<Q', nl, k * 16 + 8)[0]
            if value <= absolute and (best is None or value > best[0]):
                best = (value, strx)
        if best is None:
            return None
        self.fh.seek(SYM_BASE + self.str_off + best[1])
        sym = self.fh.read(192).split(b'\0')[0].decode('latin-1', 'replace')
        return '%s + %#x' % (sym, absolute - best[0])


def image_of(absolute):
    best = None
    for a, n in IMAGES:
        if a <= absolute:
            best = (a, n)
        else:
            break
    return best if best else (None, None)


def find_address(image_part, pattern):
    hit = [(a, n) for a, n in IMAGES if image_part in n]
    if not hit:
        raise SystemExit('no image matching %r' % image_part)
    base, name = hit[0]
    fh = SYMS.fh
    entry = SYMS.table.get(base - REGION_START)
    if not entry:
        raise SystemExit('no symbols for ' + name)
    start, count = entry
    fh.seek(SYM_BASE + SYMS.nlist_off + start * 16)
    nl = fh.read(count * 16)
    for k in range(count):
        strx = struct.unpack_from('<I', nl, k * 16)[0]
        value = struct.unpack_from('<Q', nl, k * 16 + 8)[0]
        fh.seek(SYM_BASE + SYMS.str_off + strx)
        sym = fh.read(192).split(b'\0')[0].decode('latin-1', 'replace')
        if sym == pattern:
            return value, name
    raise SystemExit('symbol not found: ' + pattern)


SYMS = Sym()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('image')
    ap.add_argument('symbol')
    ap.add_argument('--len', default='0x120')
    args = ap.parse_args()

    addr, image = find_address(args.image, args.symbol)
    length = int(args.len, 16)
    print('%s\n%#x  in %s\n' % (args.symbol, addr, image))
    code = read(addr - REGION, length)
    tmp = pathlib.Path('/tmp/fn.bin')
    tmp.write_bytes(code)
    out = subprocess.run(['aarch64-linux-gnu-objdump', '-D', '-b', 'binary', '-m', 'aarch64',
                          '--adjust-vma=%#x' % addr, str(tmp)],
                         capture_output=True, text=True).stdout
    for line in out.splitlines():
        if ':\t' not in line:
            continue
        note = ''
        m = re.search(r'\b(bl|b)\s+0x([0-9a-f]+)', line)
        if m and m.group(1) == 'bl':
            target = int(m.group(2), 16)
            name = SYMS.name_at(target)
            base, iname = image_of(target)
            if name:
                note = '   ; -> %s' % name
            elif base:
                note = '   ; -> %s + %#x' % (iname.split('/')[-1], target - base)
        print(line.rstrip() + note)


if __name__ == '__main__':
    main()
