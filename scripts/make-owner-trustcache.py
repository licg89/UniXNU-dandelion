#!/usr/bin/env python3
"""Build the trust-cache bundle this board's payload must carry, and say what it contains.

DarwinBoot validates the bundle that travels in the payload as kernel.macho.trustcache.  It has to
hold the Apple OS and SystemOS modules (the dyld shared cache lives on SystemOS, so AMFI needs its
cdhash there) plus our own fixtures' cdhashes.  Two ways to produce it:

  --from-image   take it out of a boot image that is known to boot.  This is the safe route: the
                 entries are Apple binaries' cdhashes and do not depend on our kernel (the payload
                 kernel carries no LC_CODE_SIGNATURE), so a bundle that booted stays valid.

  --merge        rebuild it from the IPSW caches with the tree's merge_trustcache.py, plus one
                 --extra=<cdhash> per fixture.  Without those extras it comes out short of what the
                 known-good bundle holds, which is why the image route is the default.

    python3 scripts/make-owner-trustcache.py --from-image D:\\aios\\out3\\boot-slide.img \\
        --out /root/work/dandelion-merged.trustcache

Then point unixnu.local.toml at it:

    owner_trust_cache = "/root/work/dandelion-merged.trustcache"

Why the owner file and not --trust-cache: unixnu/scripts/unixnu/payload.py prefers
--owner-trust-cache over --trust-cache and skips the recorded board.toml sha check for it, so an
unset or wrong owner file silently ships a bundle with only that file's entries.
"""
import argparse
import hashlib
import pathlib
import struct
import subprocess
import sys
import tempfile

PAGE_DEFAULT = 2048


def summary(blob):
    if len(blob) < 8:
        return None
    count, = struct.unpack_from('<I', blob, 0)
    if not 1 <= count <= 64:
        return None
    offsets = list(struct.unpack_from('<%dI' % count, blob, 4))
    if offsets != sorted(offsets) or offsets[0] < 4 + 4 * count:
        return None
    entries = 0
    for index, start in enumerate(offsets):
        end = offsets[index + 1] if index + 1 < len(offsets) else len(blob)
        if end - start < 24:
            return None
        version, = struct.unpack_from('<I', blob, start)
        if version != 2:
            return None
        entries += struct.unpack_from('<I', blob, start + 20)[0]
    return count, entries


def payload_from_image(image):
    data = pathlib.Path(image).read_bytes()
    if data[:8] != b'ANDROID!':
        raise SystemExit('not an Android boot image: ' + image)
    (kernel_size, _ka, ramdisk_size, _ra, _ss, _sa, _ta, page, _hv, _ov) = \
        struct.unpack_from('<10I', data, 8)
    page = page or PAGE_DEFAULT
    roff = page + (kernel_size + page - 1) // page * page
    return data[roff:roff + ramdisk_size]


def bundle_from_image(image, name='EFI/UniXNU/Payloads/kernel.macho.trustcache'):
    payload = payload_from_image(image)
    with tempfile.TemporaryDirectory() as tmp:
        payload_path = pathlib.Path(tmp) / 'payload.img'
        payload_path.write_bytes(payload)
        target = pathlib.Path(tmp) / 'out'
        target.mkdir()
        result = subprocess.run(['mcopy', '-s', '-n', '-i', str(payload_path),
                                 '::/' + name, str(target)],
                                capture_output=True, text=True)
        found = list(target.rglob(pathlib.Path(name).name))
        if not found:
            raise SystemExit('no %s inside %s: %s' % (name, image, result.stderr.strip()))
        return found[0].read_bytes()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--from-image', default=None,
                    help='boot image that is known to boot; its payload bundle is reused')
    ap.add_argument('--merge', nargs='*', default=None,
                    help='IPSW .trustcache files to merge instead (needs --tree)')
    ap.add_argument('--extra', action='append', default=[],
                    help='cdhash to add when merging (one per fixture)')
    ap.add_argument('--extras-file', default=None,
                    help='file of "<name> <sha256>" lines, e.g. /root/work/cache-cdhashes.txt: '
                         'the dyld cache set, so the kernel does not have to hash 3.7 GB page by page')
    ap.add_argument('--tree', default='/root/work/UniXNU/out/dandelion/ipados-17.7.11-21H461/dandelion/tree')
    ap.add_argument('--out', required=True)
    args = ap.parse_args()

    extras = list(args.extra)
    if args.extras_file:
        lines = pathlib.Path(args.extras_file).read_text().splitlines()
        from_file = [line.split()[1] for line in lines if len(line.split()) >= 2]
        extras += from_file
        print('extras from %s: %d' % (args.extras_file, len(from_file)))

    if args.from_image:
        blob = bundle_from_image(args.from_image)
        source = args.from_image
    elif args.merge:
        tool = pathlib.Path(args.tree) / 'scripts/merge_trustcache.py'
        if not tool.is_file():
            raise SystemExit('merge tool not found: %s' % tool)
        with tempfile.TemporaryDirectory() as tmp:
            out = pathlib.Path(tmp) / 'merged.bundle'
            cmd = ['python3', str(tool), str(out)] + list(args.merge)
            cmd += ['--extra=%s' % extra[:40] for extra in extras]
            result = subprocess.run(cmd, capture_output=True, text=True)
            if result.returncode != 0:
                raise SystemExit(result.stderr.strip() or result.stdout.strip())
            blob = out.read_bytes()
        source = 'merge of %d cache file(s) + %d extra cdhash(es)' % (len(args.merge), len(extras))
    else:
        raise SystemExit('give --from-image IMG or --merge FILE [FILE ...]')

    info = summary(blob)
    if info is None:
        raise SystemExit('the resulting bundle does not match the layout DarwinBoot validates')
    modules, entries = info
    target = pathlib.Path(args.out)
    target.write_bytes(blob)
    digest = hashlib.sha256(blob).hexdigest()
    print('source : %s' % source)
    print('bundle : %s' % target)
    print('         %d bytes, %d module(s), %d entries' % (len(blob), modules, entries))
    print('sha256 : %s' % digest)

    known_good = (62680, 3, 2608)
    if (len(blob), modules, entries) != known_good:
        print('\nnote: the bundle that booted on this board was %d bytes / %d modules / %d entries;'
              % known_good)
        print('      a different shape is not automatically wrong, but it has not booted here yet.')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
