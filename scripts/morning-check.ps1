# One command for the morning: read the kernel log of the last iOS boot and judge it.
#
#   powershell -ExecutionPolicy Bypass -File D:\aios\_work\morning-check.ps1
#
# Requires the phone in fastboot (hold power 10 s, then volume-down + power).  The script swaps the
# Android boot image back without wiping, greps the result, and says whether the log is fresh:
# the console now falls back to the driver's own transfer path, so a stale (empty) block would mean
# that fallback did not fire.
$ErrorActionPreference = 'Continue'
$serial = 'QS55XCPRCA9LDA5T'

Write-Host 'waiting for fastboot (hold power 10 s, then volume-down + power)'
$deadline = (Get-Date).AddMinutes(10)
while ((Get-Date) -lt $deadline) {
  if (@(fastboot devices 2>$null).Count -gt 0) { break }
  Start-Sleep -Seconds 5
}
if (@(fastboot devices 2>$null).Count -eq 0) { Write-Host 'phone never reached fastboot'; exit 1 }

Write-Host 'restoring the Android boot image (no wipe)'
fastboot flash boot D:\aios\artifacts\boot_backup.img 2>&1 | Select-String 'Writing'
fastboot reboot 2>&1 | Out-Null

$deadline = (Get-Date).AddMinutes(8)
while ((Get-Date) -lt $deadline) {
  if (@(adb devices 2>$null | Select-Object -Skip 1 | Where-Object { $_ -match '\sdevice$' }).Count -gt 0) { break }
  Start-Sleep -Seconds 8
}
adb -s $serial root 2>&1 | Out-Null
Start-Sleep -Seconds 3

Write-Host 'reading the console block'
adb -s $serial shell 'rm -f /data/local/tmp/klog.bin; dd if=/dev/block/mmcblk0 of=/data/local/tmp/klog.bin bs=512 skip=122139919 count=512 2>/dev/null; ls -l /data/local/tmp/klog.bin'
adb -s $serial pull /data/local/tmp/klog.bin D:\aios\klog.bin 2>&1 | Select-Object -Last 1

wsl -d Ubuntu -e bash -lc "python3 - <<'PY'
import pathlib
data = pathlib.Path('/mnt/d/aios/klog.bin').read_bytes()
nz = [i for i, b in enumerate(data) if b]
fresh = data[:8] == b'DLXLOG01' and len(nz) > 500
print('non-zero bytes:', len(nz), '| header:', data[:8])
print('LOG IS ' + ('FRESH' if fresh else 'EMPTY/STALE'))
if data[:8] == b'DLXLOG01':
    body = data[512:]
    end = body.find(b'\\x00')
    text = body[:end if end > 0 else len(body)].decode('latin-1', 'replace')
    lines = [line for line in text.splitlines() if line.strip()]
    print('lines:', len(lines))
    for line in lines[-40:]:
        print(line[:150])
    print('--- markers')
    for marker in ('external trust cache modules', 'unsuitable CT policy', 'dyld cache',
                   'Library not loaded', 'launchd', 'private/var', 'native_msdc_console_sink'):
        hits = [line for line in lines if marker in line]
        if hits:
            print(marker + ':', hits[-1][:140])
PY"
