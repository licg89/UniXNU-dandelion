#!/usr/bin/env python3
"""Find the AMFI reject site by resolving the string literal's *start*.

The first attempt searched for the address of the substring "unsuitable CT policy", but that is
in the middle of a literal like "'%s': unsuitable CT policy %d ...".  Backing up to the previous
NUL gives the address the compiler actually materialises.
"""
import pathlib
import struct
import sys

sys.path.insert(0, '/root/work/UniXNU/out/dandelion/ipados-17.7.11-21H461/dandelion/tree/kits/XnuPrelink')
from native_prelink import macho  # noqa: E402

IMAGE = pathlib.Path('/root/work/UniXNU/out/dandelion/ipados-17.7.11-21H461/dandelion/kernel/kernel.macho')
data = IMAGE.read_bytes()
image = macho(data)


def va_of(offset):
    for segment in image['segments']:
        if segment['fileoff'] <= offset < segment['fileoff'] + segment['filesize']:
            return segment['va'] + (offset - segment['fileoff'])
    return None


for needle in (b'unsuitable CT policy', b'is adhoc signed', b'shared cache'):
    at = data.find(needle)
    if at < 0:
        print(f'{needle.decode()}: not found')
        continue
    start = data.rfind(b'\0', max(0, at - 200), at) + 1
    literal = data[start:data.find(b'\0', at)]
    target = va_of(start)
    print(f'\n{literal!r}')
    print(f'  literal VA {target:#x}')
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
    print(f'  references: {len(hits)} {[hex(h) for h in hits[:4]]}')
