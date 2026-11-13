# Collect the kernel console after a UniXNU (iOS payload) boot, then symbolize the BRK records.
#
# The only step that cannot be automated: after the payload hangs or panics the phone does not
# come back to fastboot by itself.  Hold power ~10 s, then Power + Volume Down, then start this.
#
#   powershell -ExecutionPolicy Bypass -File scripts\collect-slide.ps1 -Tag slide
#
# It flashes the Android boot image back (no wipe, adb keys survive), waits for Android, pulls the
# 16 MiB flashinfo partition, decodes the console text, clears the block for the next round and
# runs scripts/brk-symbolize.py over the DANDELION-BRK lines.
param(
    [string]$Tag = 'slide',
    [string]$AndroidBoot = 'D:\aios\artifacts\boot_backup.img',
    [string]$Serial = 'QS55XCPRCA9LDA5T',
    [int]$WaitFastbootMinutes = 20,
    [int]$WaitAndroidMinutes = 8
)
$ErrorActionPreference = 'Continue'
$repo = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$raw = "D:\aios\flashinfo-$Tag-$stamp.bin"

if (@(fastboot devices 2>$null).Count -eq 0) {
    Write-Host 'not in fastboot: hold power ~10 s, then Power + Volume Down' -ForegroundColor Yellow
    $deadline = (Get-Date).AddMinutes($WaitFastbootMinutes)
    while ((Get-Date) -lt $deadline) {
        if (@(fastboot devices 2>$null).Count -gt 0) { break }
        Start-Sleep -Seconds 5
    }
}
if (@(fastboot devices 2>$null).Count -eq 0) { Write-Host 'no fastboot; giving up' -ForegroundColor Red; exit 1 }

Write-Host "flashing Android boot back: $AndroidBoot" -ForegroundColor Yellow
fastboot flash boot $AndroidBoot 2>&1 | Select-String 'Sending|Writing|Finished'
fastboot reboot 2>&1 | Out-Null

Write-Host 'waiting for Android (no wipe: adb keys stay)'
$deadline = (Get-Date).AddMinutes($WaitAndroidMinutes)
while ((Get-Date) -lt $deadline) {
    if (@(adb devices 2>$null | Select-Object -Skip 1 | Where-Object { $_ -match '\sdevice$' }).Count -gt 0) { break }
    Start-Sleep -Seconds 6
}
adb -s $Serial root 2>&1 | Out-Null
Start-Sleep -Seconds 4

Write-Host "pulling flashinfo -> $raw"
adb -s $Serial shell 'rm -f /data/local/tmp/fi.bin; dd if=/dev/block/by-name/flashinfo of=/data/local/tmp/fi.bin bs=1M 2>/dev/null'
adb -s $Serial pull /data/local/tmp/fi.bin $raw 2>&1 | Select-Object -Last 1

Write-Host 'clearing the console window so the next boot starts empty'
adb -s $Serial shell 'dd if=/dev/zero of=/dev/block/by-name/flashinfo bs=512 count=28800 conv=notrunc 2>/dev/null'

$txt = "$repo\out\console-$Tag-$stamp.txt"
wsl -d Ubuntu -e bash -lc "python3 - '$Tag' <<'PY'
import pathlib, sys
tag = sys.argv[1]
data = pathlib.Path('/mnt/d/aios/flashinfo-%s-%s.bin' % (tag, '$stamp')).read_bytes()
nz = [i for i, b in enumerate(data) if b]
print('non-zero bytes:', len(nz), 'last at', max(nz) if nz else -1)
body = data
end = body.find(b'\x00')
text = body[:end if end > 0 else len(body)].decode('latin-1', 'replace')
lines = [l for l in text.splitlines() if l.strip()]
pathlib.Path('/mnt/d/aios/_work/UniXNU-dandelion/out/console-%s-%s.txt' % (tag, '$stamp')).write_text(chr(10).join(lines), encoding='latin-1')
print('lines:', len(lines))
brk = [l for l in lines if 'DANDELION-BRK' in l]
print('DANDELION-BRK records:', len(brk))
for l in brk: print('  ' + l[:170])
print('=== tail 12')
for l in lines[-12:]: print(l[:170])
PY"

Write-Host "symbolizing $txt"
wsl -d Ubuntu -e bash -lc "python3 /mnt/d/aios/_work/UniXNU-dandelion/scripts/brk-symbolize.py /mnt/d/aios/_work/UniXNU-dandelion/out/console-$Tag-$stamp.txt --out /mnt/d/aios/_work/UniXNU-dandelion/out/brk-$Tag-$stamp.txt 2>&1 | tail -40"
