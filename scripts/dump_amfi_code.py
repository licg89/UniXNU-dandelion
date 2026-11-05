#!/usr/bin/env python3
"""Dump the AMFI code around the two message sites."""
import pathlib
import struct
import sys

sys.path.insert(0, '/root/work/UniXNU/out/dandelion/ipados-17.7.11-21H461/dandelion/tree/kits/XnuPrelink')
from native_prelink import macho, backed_offset  # noqa: E402

IMAGE = pathlib.Path('/root/work/UniXNU/out/dandelion/ipados-17.7.11-21H461/dandelion/kernel/kernel.macho')
data = IMAGE.read_bytes()
image = macho(data)


def dump(start, length, title):
    print(f'\n=== {title} ({start:#x})')
    at = backed_offset(image['segments'], start, length, len(data))
    for index in range(0, length, 4):
        word, = struct.unpack_from('<I', data, at + index)
        address = start + index
        note = ''
        # a few opcodes worth naming
        if word & 0xfc000000 == 0x14000000:
            immediate = word & 0x3ffffff
            if immediate & 0x2000000:
                immediate -= 0x4000000
            note = f'b {address + immediate * 4:#x}'
        elif word & 0xff000010 == 0x54000000:
            immediate = (word >> 5) & 0x7ffff
            if immediate & 0x40000:
                immediate -= 0x80000
            condition = word & 0xf
            note = f'b.{condition:x} {address + immediate * 4:#x}'
        elif word & 0x7f000000 in (0x71000000, 0x71000000):
            note = 'cmp imm'
        elif word & 0xff000000 == 0x94000000:
            immediate = word & 0x3ffffff
            if immediate & 0x2000000:
                immediate -= 0x4000000
            note = f'bl {address + immediate * 4:#x}'
        elif word == 0xd65f03c0:
            note = 'ret'
        print(f'  {address:#x}: {word:08x} {note}')


dump(0xfffffff004ab6080, 0x90, 'adhoc-signed message site')
dump(0xfffffff004ab62c0, 0x110, 'CT policy message site')
