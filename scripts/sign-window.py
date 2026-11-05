#!/usr/bin/env python3
"""Sign one userdata window and verify the envelope on the host.

The first attempt carved with `dd bs=4M skip=$((OFFSET / 4M))`, which is 1048576/4194304 =
0 -- so it signed the image's manifest instead of the window at +1 MiB.  Every block then
failed its hash in the kernel while the host verification passed, because both used the same
wrong carve.  This does the arithmetic explicitly, refuses a misaligned offset, and proves
the result with the kit's own OpenSSL provider before anything is flashed.

    sign-window.py --image userdata.img --name OS --offset 0x100000 --size 6077580800 \\
                   --key owner.pem --public owner.pub --uuid 04769D26-... --out dir/
"""
import argparse
import hashlib
import pathlib
import subprocess
import sys

SECTOR = 512

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--image', type=pathlib.Path, required=True)
parser.add_argument('--name', required=True)
parser.add_argument('--offset', required=True, type=lambda value: int(value, 0))
parser.add_argument('--size', required=True, type=lambda value: int(value, 0))
parser.add_argument('--key', type=pathlib.Path, required=True)
parser.add_argument('--public', type=pathlib.Path, required=True)
parser.add_argument('--uuid', required=True)
parser.add_argument('--generation', type=int, default=1)
parser.add_argument('--out', type=pathlib.Path, required=True)
parser.add_argument('--kit', type=pathlib.Path, default=pathlib.Path('kits/VerifiedRoot'))
args = parser.parse_args()

if args.offset % SECTOR or args.size % SECTOR:
    sys.exit('offset and size must be multiples of 512 bytes')
if args.offset % (1 << 20):
    sys.exit(f'offset {args.offset:#x} is not MiB aligned; check the arithmetic before carving')

carve = args.out / f'{args.name}.img'
args.out.mkdir(parents=True, exist_ok=True)
with args.image.open('rb') as source, carve.open('wb') as sink:
    source.seek(args.offset)
    remaining = args.size
    while remaining:
        chunk = source.read(min(4 << 20, remaining))
        if not chunk:
            sys.exit('image ended before the window did')
        sink.write(chunk)
        remaining -= len(chunk)
if carve.stat().st_size != args.size:
    sys.exit(f'carved {carve.stat().st_size} bytes, expected {args.size}')

with carve.open('rb') as stream:
    first = stream.read(8)
print(f'{args.name}: {args.size} bytes at {args.offset:#x}; first 8 bytes {first.hex()}')

envelope = args.out / f'{args.name}.envelope'
if envelope.is_dir():
    for old in envelope.iterdir():
        old.unlink()
subprocess.run([sys.executable, str(args.kit / 'tools/manifest.py'),
                '--image', str(carve), '--private-key', str(args.key),
                '--public-key', str(args.public), '--volume-uuid', args.uuid,
                '--generation', str(args.generation), '--output', str(envelope)], check=True,
               stdout=subprocess.DEVNULL)
manifest = envelope / 'manifest.bin'
print(f'envelope: {manifest.stat().st_size} bytes')

# verify with the kit's OpenSSL provider, so a wrong carve cannot pass silently
provider = args.out / 'libvrhost.so'
subprocess.run(['cc', '-shared', '-fPIC', '-O2', '-I', str(args.kit / 'include'),
                str(args.kit / 'src/verified_root.c'), str(args.kit / 'tests/openssl_provider.c'),
                '-lcrypto', '-o', str(provider)], check=True)
report = args.out / f'{args.name}-verification.json'
result = subprocess.run([sys.executable, str(args.kit / 'tools/verify_image.py'),
                         '--library', str(provider), '--manifest', str(manifest),
                         '--trusted-key', str(envelope / 'trusted-key.bin'), '--image', str(carve),
                         '--volume-uuid', args.uuid, '--minimum-generation', str(args.generation),
                         '--output', str(report)], capture_output=True, text=True)
print(result.stdout.strip() or result.stderr.strip())
if result.returncode:
    sys.exit('the envelope does not verify against this carve')
print(f'window hash of block 0: {hashlib.sha256(first.ljust(4096, bytes(4096))).hexdigest()[:16]}')
