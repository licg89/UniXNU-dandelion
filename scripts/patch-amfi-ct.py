#!/usr/bin/env python3
"""Relax AMFI's shared-cache CT-policy decision with a single instruction patch.

Located from the message literal: "AMFI: '%s': unsuitable CT policy %#llx ..." is materialised at
0xfffffff0042e0acb and referenced from 0xfffffff004ab634c.  The decision right above it is

    0xfffffff004ab6330  tst  x27, #0x18000001ff80
    0xfffffff004ab6334  cset w8, eq
    0xfffffff004ab6338  and  w8, w19, w8
    0xfffffff004ab633c  cmp  w8, #1
    0xfffffff004ab6340  b.ne 0xfffffff004ab6358      ; accept
    0xfffffff004ab6344  tbnz w22, #0, 0xfffffff004ab6358
    0xfffffff004ab6348  ...                          ; print the message
    0xfffffff004ab6354  b    reject
    0xfffffff004ab6358  ... accept continues here

Turning the tbnz at 0xfffffff004ab6344 into an unconditional `b +20` makes the reject block
unreachable while leaving every other check in the function untouched.  The kernel is this
tree's own build and the prelinked kexts are not signature-checked at run time, so a byte patch
after the prelink step is safe.

    patch-amfi-ct.py <kernel.macho> [--revert]

The patch is recorded so it can be verified and undone: --revert restores the original word.
"""
import argparse
import pathlib
import struct
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'kits/XnuPrelink'))
from native_prelink import macho, backed_offset  # noqa: E402

SITE = 0xfffffff004ab6344
ORIGINAL = 0x36280016   # tbnz w22, #5 ... placeholder, replaced after verification
PATCHED = 0x14000005    # b #20  -> 0xfffffff004ab6358

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('kernel', type=pathlib.Path)
parser.add_argument('--revert', action='store_true')
args = parser.parse_args()

data = bytearray(args.kernel.read_bytes())
image = macho(bytes(data))
at = backed_offset(image['segments'], SITE, 4, len(data))
current, = struct.unpack_from('<I', data, at)
print(f'{SITE:#x}: currently {current:#010x}')

if args.revert:
    if current != PATCHED:
        sys.exit('the site is not patched; nothing to revert')
    # the original is whatever the prelink produced; keep a copy so this is reversible
    backup = args.kernel.with_suffix('.ct-original')
    if not backup.is_file():
        sys.exit('no .ct-original backup to restore from')
    original, = struct.unpack_from('<I', backup.read_bytes(), at)
    struct.pack_into('<I', data, at, original)
    args.kernel.write_bytes(bytes(data))
    print(f'reverted to {original:#010x}')
    sys.exit(0)

backup = args.kernel.with_suffix('.ct-original')
if not backup.is_file():
    backup.write_bytes(args.kernel.read_bytes())
    print(f'original word saved to {backup}')
else:
    original, = struct.unpack_from('<I', backup.read_bytes(), at)
    if current != original:
        sys.exit(f'the site already differs from the original ({original:#010x}); refusing')

struct.pack_into('<I', data, at, PATCHED)
args.kernel.write_bytes(bytes(data))
print(f'patched to {PATCHED:#010x} (b #20 -> 0xfffffff004ab6358)')
