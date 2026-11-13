#!/usr/bin/env python3
"""Resolve trap addresses that came back without a usable slide.

The kernel prints `DANDELION-BRK: ... pc=... slide=... comment=...`.  When the slide is missing or
0xffffffff the address cannot be turned into an image offset directly.  This recovers it from the
cache itself:

  * the dyld shared cache is one system-wide mapping, so a single slide covers every process in a
    boot, and `pc - slide` must land on a real `brk` opcode whose immediate equals the comment;
  * `brk #imm` encodes as 0xD4200000 | (imm << 5), so the instruction at the trap site is known
    exactly;
  * with two traps that carry different comments in the same boot, the slide that satisfies both is
    normally unique.

Inputs: a raw disk image that holds the cache (the port's userdata image), the cache header file and
the local symbol table.  Output: per trap, the slide, the unslid address, the image and function.

    python3 scripts/fit-cache-slide.py --log out/console.txt \
        --image /root/work/UniXNU/out/dandelion/.../userdata/userdata-root.img \
        --header /root/work/symtab/dyld_shared_cache_arm64 \
        --symbols /root/work/symtab/dyld_shared_cache_arm64.symbols
"""
import argparse
import json
import pathlib
import re
import struct

REGION_DEFAULT = 0x180000000
GRANULE = 0x1000                 # PAGE_SIZE_FOR_SR_SLIDE in this XNU: the cache slides in 4K steps
BRK = re.compile(r'DANDELION-BRK: pid=(-?\d+) comm=(\S+) pc=(0x[0-9a-fA-F]+) '
                 r'slide=(0x[0-9a-fA-F]+) unslid=(0x[0-9a-fA-F]+) lr=(0x[0-9a-fA-F]+) '
                 r'comment=(0x[0-9a-fA-F]+)')
BRK_LEGACY = re.compile(r'DANDELION-BRK: pid=(-?\d+) comm=(\S+) pc=(0x[0-9a-fA-F]+) '
                        r'lr=(0x[0-9a-fA-F]+) comment=(0x[0-9a-fA-F]+)')


def read_header(path):
    data = pathlib.Path(path).read_bytes()
    if not data.startswith(b'dyld_v1'):
        raise SystemExit('not a dyld cache header: ' + path)
    region = struct.unpack_from('<Q', data, 0xE0)[0]
    size = struct.unpack_from('<Q', data, 0xE8)[0]
    max_slide = struct.unpack_from('<Q', data, 0xF0)[0]
    sub_off, sub_cnt = struct.unpack_from('<I', data, 0x188)[0], struct.unpack_from('<I', data, 0x18C)[0]
    shards = []
    for i in range(sub_cnt):
        rec = data[sub_off + i * 0x38: sub_off + (i + 1) * 0x38]
        shards.append((struct.unpack_from('<Q', rec, 0x10)[0], rec[:16]))
    return data, region, size, max_slide, shards


def map_files(image, shards):
    """Locate every cache file in the image and pair it with its table entry, by UUID."""
    by_uuid = {uuid: vm for vm, uuid in shards}
    text = pathlib.Path('/tmp/.cache-files.json')
    if text.exists():
        cached = json.loads(text.read_text())
        if cached.get('image') == image:
            return [(vm, 0x0, start) for vm, start in cached['files']], cached.get('main', 0)

    starts = []
    needles = b'dyld_v1   arm64'
    with open(image, 'rb') as fh:
        chunk = 1 << 24
        pos = 0
        tail = b''
        while True:
            data = fh.read(chunk)
            if not data:
                break
            blob = tail + data
            at = blob.find(needles)
            while at >= 0:
                starts.append(pos - len(tail) + at)
                at = blob.find(needles, at + 1)
            tail = blob[-len(needles):]
            pos += len(data)

    found, main = [], 0
    with open(image, 'rb') as fh:
        for off in starts:
            fh.seek(off)
            head = fh.read(0x1000)
            for vm, uuid in shards:
                if uuid in head:
                    found.append((vm, off))
                    break
    found.sort()
    text.write_text(json.dumps({'image': image, 'files': found, 'main': main}))
    return [(vm, 0x0, start) for vm, start in found], main


def find_main(image, header_bytes):
    """The main cache file is the one that starts with the header we were given."""
    with open(image, 'rb') as fh:
        pos = 0
        while True:
            data = fh.read(1 << 24)
            if not data:
                return 0
            at = data.find(b'dyld_v1   arm64')
            while at >= 0:
                fh.seek(pos + at)
                if fh.read(len(header_bytes)) == header_bytes:
                    return pos + at
                at = data.find(b'dyld_v1   arm64', at + 1)
            pos += len(data)


def scan_sites(image, header_bytes, files, main, region, size, cache):
    path = pathlib.Path(cache)
    if path.exists():
        saved = json.loads(path.read_text())
        return {int(k): v for k, v in saved.items()}
    windows = [(0, main, files[0][0] if files else size)]
    for k, (vm, _pad, start) in enumerate(files):
        nxt = files[k + 1][0] if k + 1 < len(files) else size
        windows.append((vm, start, nxt))
    sites = {}
    with open(image, 'rb') as fh:
        for lo, start, hi in windows:
            fh.seek(start)
            data = fh.read(hi - lo)
            for imm in range(0, 16):
                needle = struct.pack('<I', 0xD4200000 | (imm << 5))
                pos = data.find(needle)
                while pos >= 0:
                    if pos % 4 == 0:
                        sites.setdefault(imm, []).append(lo + pos)
                    pos = data.find(needle, pos + 1)
    path.write_text(json.dumps({str(k): v for k, v in sites.items()}))
    return sites


def traps_from_log(path):
    text = pathlib.Path(path).read_bytes().decode('latin-1', 'replace')
    out = []
    for pid, comm, pc, slide, unslid, lr, comment in BRK.findall(text):
        out.append((pid, comm, int(pc, 16), int(slide, 16), int(comment, 16)))
    if not out:
        for pid, comm, pc, lr, comment in BRK_LEGACY.findall(text):
            out.append((pid, comm, int(pc, 16), None, int(comment, 16)))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--log', required=True)
    ap.add_argument('--image', required=True)
    ap.add_argument('--header', default='/root/work/symtab/dyld_shared_cache_arm64')
    ap.add_argument('--symbols', default='/root/work/symtab/dyld_shared_cache_arm64.symbols')
    ap.add_argument('--sites-cache', default='/root/work/vr/brksites.json')
    args = ap.parse_args()

    header_bytes, region, size, max_slide, shards = read_header(args.header)
    region = region or REGION_DEFAULT
    files, _ = map_files(args.image, shards)
    main_file = find_main(args.image, header_bytes)
    if not files:
        raise SystemExit('no cache files matched the header inside ' + args.image)
    sites = scan_sites(args.image, header_bytes, files, main_file, region, size, args.sites_cache)
    known = sum(len(v) for v in sites.values())
    print('cache files: %d, brk sites indexed: %d' % (len(files), known))

    traps = traps_from_log(args.log)
    if not traps:
        raise SystemExit('no DANDELION-BRK records in ' + args.log)

    # Only addresses at or above the shared region can be cache addresses at all; a record below it
    # is in the process's own Mach-O text and needs that binary, not this cache.
    in_cache, outside = [], []
    seen = set()
    for trap in traps:
        key = (trap[1], trap[2])
        if key in seen:
            continue
        seen.add(key)
        (in_cache if trap[2] >= region else outside).append(trap)
    for pid, comm, pc, _s, comment in outside:
        print('  %-16s pc %#x is below the shared region: not a cache address' % (comm, pc))
    if not in_cache:
        raise SystemExit('no cache addresses to fit')

    need = [t for t in in_cache if t[3] is None or t[3] in (0xffffffff, 0xffffffffffffffff)]
    if not need:
        print('every record already carries a usable slide; nothing to fit')
        need = in_cache
    print('fitting %d records: %s' % (len(need), ', '.join(sorted({t[1] for t in need}))))

    def candidates(pc, comment):
        out = {}
        want = (pc - region) % GRANULE
        for off in sites.get(comment, []):
            for skew in (0, 4, -4):
                site = off + skew
                if site < 0 or site % GRANULE != want:
                    continue
                slide = pc - region - site
                if 0 <= slide < (max_slide or 0x20000000) and slide % GRANULE == 0:
                    out.setdefault(slide, []).append((site, skew))
        return out

    per = [(t, candidates(t[2], t[4])) for t in need]
    for (pid, comm, pc, _s, comment), cands in per:
        print('  %-16s pc %#x comment %d -> %d candidate slides' % (comm, pc, comment, len(cands)))

    common = None
    for _t, cands in per:
        keys = set(cands)
        common = keys if common is None else (common & keys)
    if not common:
        print('\nno single slide satisfies every record; the cache may differ from the one on the device')
        return 1

    print('\nslice(s) satisfying every record: %d' % len(common))
    symbols = None
    try:
        import importlib.util
        spec = importlib.util.spec_from_file_location(
            'brk_symbolize', pathlib.Path(__file__).with_name('brk-symbolize.py'))
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
        images, region_start = mod.read_images(args.header)
        symbols = (mod, images, mod.Symbols(args.symbols, region_start))
    except Exception as exc:                                       # noqa: BLE001
        print('(symbol table unavailable: %s)' % exc)

    for slide in sorted(common):
        print('  slide %#x' % slide)
        for (pid, comm, pc, _s, comment), _c in per:
            unslid = pc - slide
            print('     %-16s pc %#x -> unslid %#x' % (comm, pc, unslid))
            if not symbols:
                continue
            mod, images, table = symbols
            name, offset = mod.image_of(images, unslid)
            if name is None:
                print('        image: outside the shared region')
                continue
            print('        image: %s + 0x%x' % (name, offset))
            fn = table.name_at(unslid - offset, unslid)
            if fn:
                print('        symbol: %s' % fn)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
