#!/usr/bin/env python3
"""Build the writable Data seed for the payload from the iPadOS 17.7.11 (21H461) OS volume.

`unixnu build payload --data-ramdisk` wants a packed ramdisk holding one APFS
volume with the initial `/private/var` of the target OS. UniXNU ships the two
halves of this as `scripts/prepare-owner-data-fixture.py` (make the volume) and
`scripts/seed-data-grow.py` (grow an existing one), but both are pinned to the
iPadOS 17.7.11 input the project is developed against: the first refuses any
whose `sbin/launchd` does not hash to that image's, and the second grows a
packed seed this port does not have. This does the whole thing for iOS 16.5 and
prints what it made.

Every step needs macOS: `hdiutil` formats and attaches the container. Run it
there, or let `.github/workflows/data-seed.yml` run it on a hosted macOS runner.

  1. an APFS volume named OwnerData at the size asked for, holding the OS
     volume's `private/var` without `staged_system_apps`, plus the empty
     directories the userland expects to find
  2. packed with DarwinBoot's DBRDISK1 writer, so only the non-zero pages travel

The container is created at its final size rather than grown into it: APFS lays
its metadata out differently at 32 MiB and at 256 MiB, and growing from the
smaller one is an extra round of attach and resize for no benefit here.

The volume UUID is new on every run, so it goes in the report and the kernel
takes whatever volume is on the seed window rather than looking one up.

Usage:
    scripts/build-data-seed.py --os-mount /Volumes/Image \\
        --output out/nx769j-data.dbr --size-mib 256
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import plistlib
import shutil
import stat
import subprocess
import sys
from pathlib import Path

MIB = 1024 * 1024
# APFS spends a noticeable part of a small container on its own metadata, so a
# seed is only worth building when the tree it holds is comfortably smaller.
FILL_LIMIT = 0.85
# The userland expects these under the Data root even when the OS image has none.
EMPTY_DIRS = ('keybags', 'mobile/tmp', 'tmp', 'run')
# Copied into the seed but huge and replaceable on first boot.
SKIP_DIRS = ('staged_system_apps',)


def fail(message: str) -> None:
    # The workflow runner turns this into a run annotation, which is the one
    # part of a failed run that is readable without repository access.
    print(f'::error title=build-data-seed::{message}', flush=True)
    raise SystemExit('build-data-seed: ' + message)


def step(message: str) -> None:
    print(f'build-data-seed: {message}', flush=True)


def run(*args: str) -> str:
    return subprocess.run(args, check=True, capture_output=True, text=True).stdout


def packed_ramdisk(unixnu: Path | None):
    """DarwinBoot's DBRDISK1 writer.

    It lives in the DarwinBoot component, which is a submodule of UniXNU rather
    than part of it, so either the UniXNU checkout or the component on its own
    is accepted -- `git submodule update --init drivers/DarwinBoot` is enough,
    and cloning every other submodule for one file is not.
    """
    candidates = []
    for root in (unixnu, Path('../UniXNU'), Path('../DarwinBoot'),
                 Path.home() / 'build/UniXNU'):
        if root is None:
            continue
        candidates += [root / 'drivers/DarwinBoot/Test', root / 'Test', root]
    for module in candidates:
        if (module / 'packed_ramdisk.py').is_file():
            sys.path.insert(0, str(module))
            import packed_ramdisk
            return packed_ramdisk
    fail('packed_ramdisk.py not found. Point --unixnu at a UniXNU checkout whose '
         'drivers/DarwinBoot submodule is initialised, or at the DarwinBoot repo itself')


def check_mount(source: Path) -> int:
    """Refuse a mount that is not an OS volume, and report its launchd hash."""
    launchd = source / 'sbin/launchd'
    var = source / 'private/var'
    if not launchd.is_file():
        fail(f'{source} has no sbin/launchd; mount the IPSW OS volume, not another one')
    if not var.is_dir():
        fail(f'{source} has no private/var; there is no initial var tree to seed with')
    digest = hashlib.sha256(launchd.read_bytes()).hexdigest()
    entries = sum(1 for _ in var.rglob('*'))
    if entries < 2:
        fail(f'{var} holds {entries} entries, which is not an initial var tree')
    return digest


def copy_var(source: Path, target: Path) -> tuple[int, int]:
    """The OS volume's private/var, minus the directories that only bloat the seed."""
    count = total = 0
    for current, dirs, files in os.walk(source, followlinks=False):
        dirs[:] = [name for name in dirs if name not in SKIP_DIRS]
        destination = target / Path(current).relative_to(source)
        destination.mkdir(parents=True, exist_ok=True)
        os.chmod(destination, stat.S_IMODE(Path(current).stat().st_mode))
        for name in list(dirs) + files:
            here, there = Path(current) / name, destination / name
            if here.is_symlink():
                there.symlink_to(os.readlink(here))
                if name in dirs:
                    dirs.remove(name)
            elif here.is_file():
                shutil.copy2(here, there)
                count += 1
                total += here.stat().st_size
    return count, total


def attached_device(attach: dict) -> str:
    return attach['system-entities'][0]['dev-entry']


def tree_bytes(source: Path) -> int:
    """How much of private/var would actually be copied."""
    total = 0
    for current, dirs, files in os.walk(source, followlinks=False):
        dirs[:] = [name for name in dirs if name not in SKIP_DIRS]
        for name in files:
            here = Path(current) / name
            if here.is_file() and not here.is_symlink():
                total += here.stat().st_size
    return total


def verify_container(image: Path, size_mib: int) -> int:
    """The image must start with an APFS container that fills it."""
    with image.open('rb') as stream:
        header = stream.read(4096)
    if header[32:36] != b'NXSB':
        fail('the image does not start with an APFS container')
    block_size = int.from_bytes(header[36:40], 'little')
    blocks = int.from_bytes(header[40:48], 'little')
    target = size_mib * MIB
    if block_size != 4096 or blocks * block_size > target or blocks * block_size < target - 64 * MIB:
        fail(f'the container is {blocks * block_size} bytes, not the {target} asked for')
    return blocks * block_size


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--os-mount', type=Path, required=True,
                        help='the IPSW OS volume, mounted read-only')
    parser.add_argument('--output', type=Path, required=True,
                        help='where to write the packed seed (.dbr)')
    parser.add_argument('--size-mib', type=int, default=256,
                        help='Data container size in MiB (default 256, a RAM seed)')
    parser.add_argument('--unixnu', type=Path, default=None,
                        help='the UniXNU checkout holding drivers/DarwinBoot/Test/packed_ramdisk.py')
    parser.add_argument('--work', type=Path, default=None,
                        help='scratch directory (default: beside --output)')
    args = parser.parse_args()

    if args.size_mib < 32:
        fail(f'--size-mib {args.size_mib} is too small to hold an APFS container')
    if args.output.exists():
        fail(f'{args.output} exists; this tool never overwrites a seed')

    source = args.os_mount.resolve()
    if not source.is_dir():
        fail(f'{source} is not a directory')
    step(f'checking {source} is an iOS OS volume')
    launchd_hash = check_mount(source)

    tree = source / 'private/var'
    needed = tree_bytes(tree)
    budget = args.size_mib * MIB
    step(f'private/var holds {needed} bytes to copy; the container is {budget} bytes')
    if needed > budget * FILL_LIMIT:
        fail(f'private/var is {needed} bytes and --size-mib {args.size_mib} gives {budget} '
             f'before APFS metadata; raise --size-mib')

    args.output.parent.mkdir(parents=True, exist_ok=True)
    work = (args.work or args.output.parent / 'data-seed-work').resolve()
    work.mkdir(parents=True, exist_ok=True)
    image = work / 'owner-data.cdr'
    mount = work / 'mount'
    if image.exists():
        fail(f'{image} exists; clear the scratch directory first')
    packer = packed_ramdisk(args.unixnu.resolve() if args.unixnu else None)

    step(f'creating a {args.size_mib} MiB APFS OwnerData volume at {image}')
    run('hdiutil', 'create', '-size', f'{args.size_mib}m', '-layout', 'NONE', '-fs', 'APFS',
        '-volname', 'OwnerData', '-type', 'UDTO', str(image))
    if not image.is_file():
        fail(f'hdiutil did not write {image}')
    mount.mkdir(exist_ok=True)
    attached = plistlib.loads(subprocess.check_output(
        ['hdiutil', 'attach', '-nobrowse', '-owners', 'off', '-mountpoint', str(mount),
         '-plist', str(image)]))
    device = attached_device(attached)
    volume_uuid = None
    try:
        volumes = [e for e in attached['system-entities'] if e.get('mount-point') == str(mount)]
        if len(volumes) != 1:
            fail('expected exactly one newly formatted scratch volume')
        info = plistlib.loads(subprocess.check_output(
            ['diskutil', 'info', '-plist', volumes[0]['dev-entry']]))
        volume_uuid = info['VolumeUUID']
        step('copying private/var into the seed')
        count, total = copy_var(source / 'private/var', mount)
        for name in EMPTY_DIRS:
            (mount / name).mkdir(parents=True, exist_ok=True)
        os.chmod(mount / 'tmp', 0o1777)
    finally:
        subprocess.run(['hdiutil', 'detach', device], check=True)

    container_bytes = verify_container(image, args.size_mib)
    step('packing the seed')
    metadata = packer.pack(image, args.output)

    report = {
        'os_mount': str(source),
        'os_launchd_sha256': launchd_hash,
        'volume_uuid': volume_uuid,
        'size_mib': args.size_mib,
        'container_bytes': container_bytes,
        'files': count,
        'file_bytes': total,
        'skipped_dirs': list(SKIP_DIRS),
        'output': str(args.output),
        'output_sha256': hashlib.sha256(args.output.read_bytes()).hexdigest(),
        'output_bytes': args.output.stat().st_size,
        'hardware_tested': False,
        **metadata,
    }
    report_path = Path(str(args.output) + '.json')
    report_path.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report, indent=2))
    print(f'\nbuild-data-seed: {args.output} ({args.output.stat().st_size} bytes)')
    print(f'  Data volume UUID {volume_uuid}')
    print(f'  pass it as: ./unixnu build payload --data-ramdisk {args.output}')
    return 0


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except SystemExit:
        raise
    except Exception as error:  # noqa: BLE001 - the annotation is the point
        print(f'::error title=build-data-seed::{type(error).__name__}: {error}', flush=True)
        raise
