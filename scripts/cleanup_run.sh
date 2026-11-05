#!/bin/bash
# Clean the experiment leftovers, keeping the images that matter.
set -u
OUT=/mnt/d/aios/out3
before=$(df --output=avail -k /mnt/d | tail -1)

echo '=== keeping'
KEEP="boot-baseline.img boot-ctpatch.img userdata-root.img userdata-signed.img apple-trustcache.bundle"
for name in $KEEP; do
  [ -e "$OUT/$name" ] && echo "  $name"
done
echo '  vr/ (signed envelopes and keys)'
echo '  gpt-dump.bin, flashinfo.bin'

echo '=== removing experiment boot images'
count=0
for image in "$OUT"/boot-*.img; do
  base=$(basename "$image")
  case " $KEEP " in
    *" $base "*) continue ;;
  esac
  rm -f "$image" && count=$((count + 1))
done
echo "  removed $count boot images"

echo '=== removing rebuildable windows and obsolete payloads'
for name in os-window-exact.img systemos-window.img userdata-manifest-64m.img \
            payload.img payload-window.img esp-small.img; do
  [ -e "$OUT/$name" ] && rm -f "$OUT/$name" && echo "  removed $name"
done

echo '=== removing WSL scratch'
rm -f /root/work/systemos-apfs.img /root/work/systemos-cryptex.img /root/work/cryptex-apfs.img
rm -rf /root/work/payload-check /root/work/vrcheck
rm -f /root/work/*.log
rm -f /root/work/bsd_init-*.c /root/work/kern_exec*.c /root/work/series.bak* /root/work/root-authorize.patch /root/work/console-gate.patch
echo '  done'

after=$(df --output=avail -k /mnt/d | tail -1)
echo "=== D: reclaim $(( (after - before) / 1024 )) MiB"
df -h /mnt/d | tail -1
du -sh "$OUT" 2>/dev/null
ls -S "$OUT" | head -6
