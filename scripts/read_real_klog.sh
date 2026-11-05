#!/bin/bash
# The console writes at an ABSOLUTE device sector, not at partition-relative 30000.  Read it from
# the raw device instead of from the flashinfo partition.
set -u
LOG_LBA=$((0x747b50f))
echo "console log-lba: $LOG_LBA (0x$(printf '%x' $LOG_LBA))"
adb -s QS55XCPRCA9LDA5T shell "rm -f /data/local/tmp/klog.bin; dd if=/dev/block/mmcblk0 of=/data/local/tmp/klog.bin bs=512 skip=$LOG_LBA count=512 2>/dev/null; ls -l /data/local/tmp/klog.bin" 2>&1 | tail -2
adb -s QS55XCPRCA9LDA5T pull /data/local/tmp/klog.bin /mnt/d/aios/klog-ondevice.bin 2>&1 | tail -1
python3 - <<'PY'
import pathlib
data = pathlib.Path('/mnt/d/aios/klog-ondevice.bin').read_bytes()
nz = [i for i, b in enumerate(data) if b]
print('bytes:', len(data), 'non-zero:', len(nz), 'last at', max(nz) if nz else -1)
print('first 16:', data[:16].hex())
if data[:8] == b'DLXLOG01':
    body = data[512:]
    end = body.find(b'\x00')
    text = body[:end if end > 0 else len(body)].decode('latin-1', 'replace')
    lines = [line for line in text.splitlines() if line.strip()]
    print('lines:', len(lines))
    for line in lines[-18:]:
        print(line[:150])
PY
