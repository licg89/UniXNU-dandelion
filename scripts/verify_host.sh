#!/bin/bash
# Verify our signed envelope against our exact OS carve on the host, with the kit's own
# OpenSSL provider.  This settles whether the signing is right without touching the phone:
# if this passes, the kernel side (the bytes it reads, or its SHA-256 adapter) is at fault.
set -eu
K=/root/work/UniXNU/kits/VerifiedRoot
W=/mnt/d/aios/out3
OUT=/root/work/vrcheck
mkdir -p "$OUT"

echo '=== building the host provider'
cc -shared -fPIC -O2 -I "$K/include" \
   "$K/src/verified_root.c" "$K/tests/openssl_provider.c" \
   -lcrypto -o "$OUT/libvrhost.so" 2>&1 | tail -5
ls -l "$OUT/libvrhost.so"

echo '=== verifying every block of the exact carve'
python3 "$K/tools/verify_image.py" \
  --library "$OUT/libvrhost.so" \
  --manifest "$W/vr/os-exact.envelope/manifest.bin" \
  --trusted-key "$W/vr/os-exact.envelope/trusted-key.bin" \
  --image "$W/os-window-exact.img" \
  --volume-uuid 04769d26-2ca0-4825-b443-e2e5f88300f2 \
  --minimum-generation 1 \
  --output "$OUT/report.json" 2>&1 | tail -6
cat "$OUT/report.json" 2>/dev/null | head -12
