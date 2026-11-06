# One command for the morning: pull the last boot's console log and print the carrier verdict.
#
#   1. hold power 10 s, then volume-down + power  (so the phone reaches fastboot)
#   2. run:  powershell -ExecutionPolicy Bypass -File D:\aios\_work\morning-carrier.ps1
#
# The phone is returned to Android without wiping, so it stays usable afterwards.
$ErrorActionPreference = 'Continue'
$serial = 'QS55XCPRCA9LDA5T'

Write-Host 'waiting for fastboot (hold power 10 s, then volume-down + power)'
$deadline = (Get-Date).AddMinutes(15)
while ((Get-Date) -lt $deadline) {
  if (@(fastboot devices 2>$null).Count -gt 0) { break }
  Start-Sleep -Seconds 5
}
if (@(fastboot devices 2>$null).Count -eq 0) { Write-Host 'phone never reached fastboot'; exit 1 }

Write-Host 'restoring Android boot (no wipe)'
fastboot flash boot D:\aios\artifacts\boot_backup.img 2>&1 | Select-String 'Writing'
fastboot reboot 2>&1 | Out-Null

$deadline = (Get-Date).AddMinutes(8)
while ((Get-Date) -lt $deadline) {
  if (@(adb devices 2>$null | Select-Object -Skip 1 | Where-Object { $_ -match '\sdevice$' }).Count -gt 0) { break }
  Start-Sleep -Seconds 8
}
adb -s $serial root 2>&1 | Out-Null
Start-Sleep -Seconds 3
adb -s $serial shell 'dd if=/dev/block/by-name/flashinfo of=/data/local/tmp/fi.bin bs=1M 2>/dev/null'
adb -s $serial pull /data/local/tmp/fi.bin D:\aios\flashinfo.bin 2>&1 | Select-Object -Last 1
adb -s $serial shell 'dd if=/dev/zero of=/dev/by-name/flashinfo bs=512 count=28800 conv=notrunc 2>/dev/null'

wsl -d Ubuntu -e bash -lc "python3 - <<'PY'
import pathlib
d = pathlib.Path('/mnt/d/aios/flashinfo.bin').read_bytes()
body = d[:16 << 20][512:]
end = body.find(b'\x00')
text = body[:end if end > 0 else len(body)].decode('latin-1', 'replace')
lines = [l for l in text.splitlines() if l.strip()]
print('console lines:', len(lines))
print()
print('=== the carrier verdict (what we are here for)')
for l in lines:
    if 'D4K carrier' in l or 'D4K owner' in l or 'D4K paths' in l:
        print('  ', l[:200])
print()
print('=== owner / cache / dyld / launchd')
for l in lines:
    if any(k in l for k in ('OwnerData', 'preboot', 'Cryptex', 'dyld', 'cache_status',
                            'load_init_program', 'shared_region', 'libSystem', 'panic')):
        print('  ', l[:170])
print()
print('=== tail 16')
for l in lines[-16:]:
    print(l[:170])
PY"
