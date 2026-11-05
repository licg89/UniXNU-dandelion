#!/usr/bin/env python3
"""Build the VerifiedRoot archive: the verifier, the XNU crypto adapter and the owner key.

The driver declares `extern const uint8_t verified_root_key[]` and expects exactly
VR_KEY_BYTES (395) bytes of anchor, which tools/manifest.py derives from the owner
public key.  The verifier itself is kits/VerifiedRoot/src/verified_root.c and its
crypto comes from kits/VerifiedRoot/adapters/xnu_crypto.c (the kernel's own SHA-256
and RSA-3072), so this archive is what makes NATIVE_VERIFIED_BLOCK=1 link.

    build-verified-root.py --key /root/work/vr/trusted-key.bin
"""
import argparse
import json
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--key', type=pathlib.Path, required=True, help='the 395-byte trusted-key.bin')
parser.add_argument('--out', type=pathlib.Path, default=ROOT / 'build/verified-root')
args = parser.parse_args()

out = args.out.resolve()
out.mkdir(parents=True, exist_ok=True)

kit = ROOT / 'kits/VerifiedRoot'
sources = [kit / 'src/verified_root.c', kit / 'adapters/xnu_crypto.c']
for source in sources:
    if not source.is_file():
        sys.exit(f'missing {source}; is the VerifiedRoot component fetched?')
key = args.key.read_bytes()
if len(key) != 395:
    sys.exit(f'trusted key must be 395 bytes, got {len(key)}')

record_path = ROOT / 'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/iokit/DEVELOPMENT/IOPlatformExpert.cpo.json'
record = json.loads(record_path.read_text().strip().rstrip(','))


def compile_source(source, obj):
    command, index = [], 0
    arguments = record['arguments']
    # The record is a C++ command line; these sources are C.  '-x c' has to come
    # before the input file, otherwise clang keeps compiling them as C++ and a
    # file-scope `const` (verified_root_key, vr_xnu_crypto) gets internal linkage and
    # never becomes a symbol.  The C++-only switches then have to go.
    language = 'c' if source.suffix == '.c' else 'c++'
    cxx_only = ('-std=', '--driver-mode=', '-fsized-deallocation',
                '-fc++-static-destructors', '-fno-c++-static-destructors')
    while index < len(arguments):
        arg = arguments[index]
        if arg == record['file']:
            command += ['-x', language, str(source)]
        elif language == 'c' and arg.startswith(cxx_only):
            pass
        elif arg == '-o':
            command += ['-o', str(obj)]
            index += 1
        elif arg == '-x':
            index += 1
        elif arg == '-Xclang' and arguments[index + 1].startswith('-W'):
            index += 1
        elif not arg.startswith('-W'):
            command.append(arg)
        index += 1
    command += ['-Wall', '-Wextra', '-Wno-unused-parameter',
                '-Wno-missing-designated-field-initializers', '-Wno-unknown-warning-option',
                '-I', str(kit / 'include')]
    result = subprocess.run(command, cwd=record['directory'], stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True)
    (out / (obj.stem + '.log')).write_text(result.stdout)
    if result.returncode != 0:
        sys.stderr.write(result.stdout[-4000:])
        sys.exit(f'{source.name} did not compile')


objects = []
for source in sources:
    obj = out / (source.stem + '.o')
    compile_source(source, obj)
    objects.append(obj)
    print(f'compiled {source.name}')

# the anchor as a linkable symbol
key_body = ', '.join(f'{byte:#04x}' for byte in key)
(out / 'verified_root_key.c').write_text(
    '/* The owner key tools/manifest.py derived from owner.pub: exactly VR_KEY_BYTES. */\n'
    '#include <stdint.h>\n'
    f'const uint8_t verified_root_key[395] = {{ {key_body} }};\n'
    '\n'
    '/* The transport that answers the owner-root query links a policy bundle of its own.\n'
    ' * It never probes on this SoC (no virtio-mmio node), so an empty bundle satisfies the\n'
    ' * reference without inventing policy: the eMMC windows carry the real one. */\n'
    'const uint8_t verified_root_manifest[1] = { 0 };\n'
    'const uint64_t verified_root_manifest_bytes = 0;\n')
compile_source(out / 'verified_root_key.c', out / 'verified_root_key.o')
objects.append(out / 'verified_root_key.o')
print('compiled the owner key')

archive = out / 'libVerifiedRoot.a'
archive.unlink(missing_ok=True)
subprocess.run(['llvm-ar', '--format=darwin', 'rcs', str(archive), *[str(o) for o in objects]], check=True)
print(f'PASS: {archive} ({archive.stat().st_size} bytes)')
