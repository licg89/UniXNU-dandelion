# Restore the Android boot image WITHOUT wiping, pull the kernel console block, then clear it.
#
# The console now writes from the start of the flashinfo partition (device sector 122,109,919) with
# a 28,672-sector window, so the whole boot fits and the log is read from the partition start.
$ErrorActionPreference = 'Continue'
$serial = 'QS55XCPRCA9LDA5T'

Write-Host 'waiting for fastboot'
$deadline = (Get-Date).AddMinutes(6)
while ((Get-Date) -lt $deadline) {
  if (@(fastboot devices 2>$null).Count -gt 0) { break }
  Start-Sleep -Seconds 5
}
fastboot flash boot D:\aios\artifacts\boot_backup.img 2>&1 | Select-String 'Writing'
fastboot reboot 2>&1 | Out-Null

Write-Host 'waiting for Android (no wipe, adb keys stay)'
$deadline = (Get-Date).AddMinutes(7)
while ((Get-Date) -lt $deadline) {
  if (@(adb devices 2>$null | Select-Object -Skip 1 | Where-Object { $_ -match '\sdevice$' }).Count -gt 0) { break }
  Start-Sleep -Seconds 8
}
adb -s $serial root 2>&1 | Out-Null
Start-Sleep -Seconds 3

Write-Host 'pulling the kernel console block (partition start)'
adb -s $serial shell 'rm -f /data/local/tmp/fi.bin; dd if=/dev/block/by-name/flashinfo of=/data/local/tmp/fi.bin bs=1M 2>/dev/null'
adb -s $serial pull /data/local/tmp/fi.bin D:\aios\flashinfo.bin 2>&1 | Select-Object -Last 1

Write-Host 'clearing the block so the next boot starts empty'
adb -s $serial shell 'dd if=/dev/zero of=/dev/block/by-name/flashinfo bs=512 count=28800 conv=notrunc 2>/dev/null'

wsl -d Ubuntu -e bash -lc "python3 - <<'PY'
import pathlib
data = pathlib.Path('/mnt/d/aios/flashinfo.bin').read_bytes()
nz = [i for i, b in enumerate(data[:16 << 20]) if b]
print('non-zero bytes in the console window:', len(nz), 'last at', max(nz) if nz else -1)
print('header:', data[:16])
body = data[512:]
end = body.find(b'\x00')
text = body[:end if end > 0 else len(body)].decode('latin-1', 'replace')
lines = [line for line in text.splitlines() if line.strip()]
print('lines:', len(lines))
print('=== markers')
for marker in ('DANDELION-EXEC', 'unsuitable CT policy', 'dyld cache', 'Library not loaded',
               'launchd', 'private/var', 'mount', 'exec'):
    hits = [line for line in lines if marker in line]
    for line in hits[-3:]:
        print(f'  [{marker}] {line[:150]}')
print('=== tail 24')
for line in lines[-24:]:
    print(line[:160])
PY"
