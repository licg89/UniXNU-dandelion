#!/usr/bin/env python3
"""Repair only the two system watchdog policy files on a copy of a 512 MiB seed."""
import argparse
from contextlib import contextmanager
import json
import os
from pathlib import Path
import plistlib
import re
import stat
import subprocess
import sys
import tempfile
from uuid import UUID

SIZE = 512 * 1024 * 1024
NAMES = ('disabled.plist', 'disabled.migrated')
LAUNCHD_SHA = '8cec9a3c44a443b08e977ccc5d577850316ad428c575dc01f09b018dbd5b6a25'


def check(condition, message):
    if not condition:
        raise ValueError(message)


def unpack(packer, seed, raw):
    logical, extents = packer.regions(seed.read_bytes())
    check(logical == SIZE, 'expected 512 MiB logical disk')
    with raw.open('xb') as stream:
        stream.truncate(logical)
        for offset, data in extents:
            stream.seek(offset)
            stream.write(data)


def run(*args):
    return subprocess.run(args, check=True, stdout=subprocess.PIPE).stdout


@contextmanager
def mounted(raw, mount, uuid, readonly=False):
    mount.mkdir()
    args = ['hdiutil', 'attach', '-nobrowse', '-owners', 'on', '-plist',
            '-imagekey', 'diskimage-class=CRawDiskImage', '-mountpoint', str(mount)]
    if readonly:
        args.append('-readonly')
    entities = plistlib.loads(run(*args, str(raw)))['system-entities']
    root = next((e['dev-entry'] for e in entities
                 if re.fullmatch(r'/dev/disk\d+', e.get('dev-entry', ''))), None)
    check(root is not None, 'attach did not identify the whole attached disk')
    try:
        volumes = [e for e in entities if e.get('mount-point') == str(mount)]
        check(len(volumes) == 1, 'expected one mounted volume in the scratch directory')
        info = plistlib.loads(run('diskutil', 'info', '-plist', volumes[0]['dev-entry']))
        check(info.get('GlobalPermissionsEnabled') is True, 'volume ownership not enabled')
        check(UUID(info['VolumeUUID']) == uuid, 'APFS volume UUID differs from seed report')
        yield mount
    finally:
        run('hdiutil', 'detach', root)


def policy_files(mount):
    directory = mount
    for name in ('db', 'com.apple.xpc.launchd'):
        directory /= name
        check(stat.S_ISDIR(directory.lstat().st_mode), f'not a real directory: {directory}')
    files = [directory / name for name in NAMES]
    for path in files:
        st = path.lstat()
        check(stat.S_ISREG(st.st_mode) and st.st_nlink == 1,
              f'expected a regular, single-link file: {path}')
    contents = tuple(path.read_bytes() for path in files)
    policy = plistlib.loads(contents[0])
    check(isinstance(policy, dict) and set(policy) == {'com.apple.watchdogd'}
          and policy['com.apple.watchdogd'] is True and contents[1] == b'',
          'expected the exact watchdog-only dictionary and empty marker')
    return files, contents


def audit(files, verify=False):
    result = {}
    for path in files:
        st = path.lstat()
        mode = stat.S_IMODE(st.st_mode)
        result[path.name] = dict(uid=st.st_uid, gid=st.st_gid, mode=f'{mode:04o}', size=st.st_size)
        if verify:
            check((st.st_uid, st.st_gid, mode) == (0, 0, 0o644),
                  f'{path.name}: expected 0:0 mode 0644, got {result[path.name]}')
    return result


def metadata(packer, seed):
    result, header = packer.inspect(seed)
    check(result['packed'] is True and result['bytes'] == SIZE and header[32:36] == b'NXSB'
          and int.from_bytes(header[36:40], 'little') == 4096
          and int.from_bytes(header[40:48], 'little') * 4096 == SIZE,
          'expected a packed 512 MiB APFS container with 4 KiB blocks')
    return dict(result, output_sha256=result['encoded_sha256'], output_bytes=result['encoded_bytes'])


def repair(packer, seed, report_path, backup, work):
    check(not backup.is_relative_to(seed.parent), 'backup belongs outside the artifact directory')
    report = json.loads(report_path.read_text(encoding='utf-8'))
    original = metadata(packer, seed)
    check(report['size_mib'] == 512 and report['container_bytes'] == SIZE, 'wrong report size')
    check(report['os_launchd_sha256'] == LAUNCHD_SHA, 'unexpected launchd build')
    check(all(report.get(k) == v for k, v in original.items()), 'seed/report identity mismatch')
    uuid = UUID(report['volume_uuid'])
    with tempfile.TemporaryDirectory(prefix='seed-ownership-', dir=work) as td:
        scratch = Path(td)
        raw = scratch / 'owner-data.cdr'
        unpack(packer, seed, raw)
        if backup.exists():
            previous = report['watchdog_policy_ownership_repair']
            check(metadata(packer, backup)['output_sha256'] == previous['original_output_sha256'],
                  'backup identity mismatch')
            with mounted(raw, scratch / 'verify', uuid, readonly=True) as mount:
                files, _ = policy_files(mount)
                check(audit(files, verify=True) == previous['files'], 'stored ownership audit mismatch')
            return report

        backup.parent.mkdir(parents=True, exist_ok=True)
        with backup.open('xb') as stream:
            stream.write(seed.read_bytes())
            stream.flush()
            os.fsync(stream.fileno())
        check(metadata(packer, backup) == original, 'backup identity mismatch')
        with mounted(raw, scratch / 'repair', uuid) as mount:
            files, contents = policy_files(mount)
            before = audit(files)
            for path in files:
                run('sudo', '-n', 'chown', '0:0', str(path))
                run('sudo', '-n', 'chmod', '0644', str(path))
            audit(files, verify=True)
            run('sync')

        with tempfile.TemporaryDirectory(prefix='.owned-', dir=seed.parent) as staging:
            staged = Path(staging)
            packed = staged / seed.name
            packer.pack(raw, packed)
            identity = metadata(packer, packed)
            check_raw = scratch / 'repacked.cdr'
            unpack(packer, packed, check_raw)
            with mounted(check_raw, scratch / 'verify', uuid, readonly=True) as mount:
                files, persisted = policy_files(mount)
                check(persisted == contents, 'policy contents changed')
                after = audit(files, verify=True)
            report.update(identity, watchdog_policy_ownership_repair={
                'status': 'verified-on-readonly-remount-of-repacked-image',
                'original_output_sha256': original['output_sha256'],
                'original_output_bytes': original['output_bytes'],
                'volume_uuid': str(uuid).upper(), 'before': before, 'files': after,
            })
            new_report = staged / report_path.name
            new_report.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
            os.replace(packed, seed)
            os.replace(new_report, report_path)
            check(metadata(packer, seed) == identity, 'published seed identity mismatch')
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for arg in ('seed', 'report', 'backup', 'unixnu', 'work'):
        parser.add_argument('--' + arg, type=Path, required=True)
    args = parser.parse_args()
    check(sys.platform == 'darwin', 'APFS repair requires macOS')
    component = args.unixnu.resolve() / 'drivers/DarwinBoot/Test'
    check((component / 'packed_ramdisk.py').is_file(), 'initialise the DarwinBoot submodule first')
    sys.path.insert(0, str(component))
    import packed_ramdisk
    seed, report = args.seed.resolve(), args.report.resolve()
    check(report == seed.with_name(seed.name + '.json'), 'report must be adjacent to seed')
    args.work.mkdir(parents=True, exist_ok=True)
    print(json.dumps(repair(packed_ramdisk, seed, report, args.backup.resolve(),
                            args.work.resolve()), indent=2))


if __name__ == '__main__':
    main()
