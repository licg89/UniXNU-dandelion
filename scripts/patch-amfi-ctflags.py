"""Data-level patch: force AMFI's platform-flag answer instead of skipping the branch.

The decision is

    0xfffffff004ab6330  tst  x27, #0x18000001ff80     ; any platform-class cd flag?
    0xfffffff004ab6334  cset w8, eq                   ; w8 = "none set"
    0xfffffff004ab6338  and  w8, w19, w8              ; && the image is adhoc
    0xfffffff004ab633c  cmp  w8, #1
    0xfffffff004ab6340  b.ne accept

Replacing the cset with `mov w8, #0` makes the combined test false, so the accept branch is
taken while every other instruction still runs.  That is why this is safer than the earlier
`b` patch, which skipped code and left the boot hanging in APFS init.
"""
import argparse
import pathlib
import struct
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'kits/XnuPrelink'))
from native_prelink import macho, backed_offset  # noqa: E402

SITE = 0xfffffff004ab6334
PATCHED = 0x52800008   # mov w8, #0

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('kernel', type=pathlib.Path)
parser.add_argument('--revert', action='store_true')
args = parser.parse_args()

data = bytearray(args.kernel.read_bytes())
image = macho(bytes(data))
at = backed_offset(image['segments'], SITE, 4, len(data))
current, = struct.unpack_from('<I', data, at)
backup = args.kernel.with_suffix('.ctflags-original')

if args.revert:
    if not backup.is_file():
        sys.exit('no .ctflags-original backup')
    original, = struct.unpack_from('<I', backup.read_bytes(), at)
    struct.pack_into('<I', data, at, original)
    args.kernel.write_bytes(bytes(data))
    print(f'reverted to {original:#010x}')
    raise SystemExit(0)

print(f'{SITE:#x}: currently {current:#010x}')
if current == PATCHED:
    print('already patched')
    raise SystemExit(0)
if not backup.is_file():
    backup.write_bytes(args.kernel.read_bytes())
    print(f'original saved to {backup}')
struct.pack_into('<I', data, at, PATCHED)
args.kernel.write_bytes(bytes(data))
print(f'patched to {PATCHED:#010x} (mov w8, #0)')
