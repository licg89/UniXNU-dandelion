#!/usr/bin/env python3
"""Sanity-check the reference scanner on a string we know is referenced.

Our own dandelion-exec-diag.patch prints "DANDELION-EXEC: load_machfile ...", so the compiled
kernel must contain an adrp/add pair that materialises that string.  If the scanner finds it,
the earlier "0 references" result for AMFI's message means the string really is orphaned in this
image rather than the scan being broken.
"""
import pathlib
import struct
import sys

sys.path.insert(0, '/root/work/UniXNU/out/dandelion/ipados-17.7.11-21H461/dandelion/tree/kits/XnuPrelink')
from native_prelink import macho  # noqa: E402

IMAGE = pathlib.Path('/root/work/UniXNU/out/dandelion/ipados-17.7.11-21H461/dandelion/kernel/kernel.macho')
data = IMAGE.read_bytes()
image = macho(data)


def locate(needle):
    at = data.find(needle)
    if at < 0:
        return None, None
    for segment in image['segments']:
        if segment['fileoff'] <= at < segment['fileoff'] + segment['filesize']:
            return at, segment['va'] + (at - segment['fileoff'])
    return at, None


def references(target):
    page = target & ~0xfff
    hits = []
    for segment in image['segments']:
        if not segment['initprot'] & 4:
            continue
        blob = data[segment['fileoff']:segment['fileoff'] + segment['filesize']]
        for offset in range(0, len(blob) - 8, 4):
            word, = struct.unpack_from('<I', blob, offset)
            if word & 0x9f00001f != 0x90000000:
                continue
            immediate = ((word >> 5 & 0x7ffff) << 2) | (word >> 29 & 3)
            if immediate & 0x100000:
                immediate -= 0x200000
            if ((segment['va'] + offset) & ~0xfff) + immediate * 4096 != page:
                continue
            second, = struct.unpack_from('<I', blob, offset + 4)
            if (second & 0x7f000000) in (0x11000000, 0x31000000, 0x51000000):
                shift = (second >> 22) & 1
                if page + ((second >> 10 & 4095) << (12 if shift else 0)) == target:
                    hits.append(segment['va'] + offset)
    return hits


for needle in (b'DANDELION-EXEC: load_machfile', b'unsuitable CT policy', b'is adhoc signed'):
    at, va = locate(needle)
    if va is None:
        print(f'{needle.decode():32s} not found')
        continue
    hits = references(va)
    print(f'{needle.decode():32s} VA {va:#x}  references: {len(hits)}'
          + (f'  first {hits[0]:#x}' if hits else ''))
