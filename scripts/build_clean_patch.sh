#!/bin/bash
# The framework re-uses .cache/xnu-work, so an edited patch is never re-applied: the kernel kept
# the previous diagnostic while the build reported the patch as applied.  Wipe the work tree so
# the sources are re-extracted and every patch is applied from the current file contents.
set -u
W=/root/work/UniXNU/out/dandelion/ipados-17.7.11-21H461/dandelion
echo '=== before'
python3 - <<'PY'
import pathlib
k = pathlib.Path('/root/work/UniXNU/out/dandelion/ipados-17.7.11-21H461/dandelion/kernel/kernel.macho')
if k.is_file():
    d = k.read_bytes()
    print('  D4K perms:', d.count(b'D4K perms'), ' D4K launchd:', d.count(b'D4K launchd'))
PY
rm -rf "$W/tree/.cache/xnu-work"
echo '=== work tree removed'
bash /mnt/d/aios/_work/build_data_volume.sh 2>&1 | tail -6
echo '=== after'
python3 - <<'PY'
import pathlib
k = pathlib.Path('/root/work/UniXNU/out/dandelion/ipados-17.7.11-21H461/dandelion/kernel/kernel.macho')
d = k.read_bytes()
print('  D4K perms:', d.count(b'D4K perms'), ' D4K launchd:', d.count(b'D4K launchd'))
PY
