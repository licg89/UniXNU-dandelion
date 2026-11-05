# Restore the Android boot image WITHOUT wiping, pull the kernel log, then clear it.
#
# The log block is never rewritten from scratch by the console, so a run that writes nothing
# leaves the previous text in place and a stale log looks like a fresh one.  Clearing the block
# after each pull means the next boot starts empty: an empty read is a signal in itself.
$ErrorActionPreference = 'Continue'
$serial = 'QS55XCPRCA9LDA5T'

Write-Host 'waiting for fastboot'
$deadline = (Get-Date).AddMinutes(5)
while ((Get-Date) -lt $deadline) {
  if (@(fastboot devices 2>$null).Count -gt 0) { break }
  Start-Sleep -Seconds 5
}
fastboot flash boot D:\aios\artifacts\boot_backup.img 2>&1 | Select-String 'Writing'
fastboot reboot 2>&1 | Out-Null

Write-Host 'waiting for Android (no wipe, adb keys stay)'
$deadline = (Get-Date).AddMinutes(6)
while ((Get-Date) -lt $deadline) {
  if (@(adb devices 2>$null | Select-Object -Skip 1 | Where-Object { $_ -match '\sdevice$' }).Count -gt 0) { break }
  Start-Sleep -Seconds 8
}
adb -s $serial root 2>&1 | Out-Null
Start-Sleep -Seconds 3

Write-Host 'pulling the kernel log'
adb -s $serial shell 'rm -f /data/local/tmp/fi.bin; dd if=/dev/block/by-name/flashinfo of=/data/local/tmp/fi.bin bs=1M 2>/dev/null'
adb -s $serial pull /data/local/tmp/fi.bin D:\aios\flashinfo.bin 2>&1 | Select-Object -Last 1

Write-Host 'clearing the log block so the next boot starts empty'
adb -s $serial shell 'dd if=/dev/zero of=/dev/block/by-name/flashinfo bs=512 seek=30000 count=512 conv=notrunc 2>/dev/null'

wsl -d Ubuntu -e bash -lc "python3 /mnt/d/aios/_work/klog_extract.py /mnt/d/aios/flashinfo.bin 30000 512 /mnt/d/aios/_work/klog-last2.txt >/dev/null; echo '=== driver and policy'; grep -a 'NativeMsdc' /mnt/d/aios/_work/klog-last2.txt; echo '=== trust cache modules'; grep -a 'external trust cache modules' /mnt/d/aios/_work/klog-last2.txt; echo '=== cache / systemos / launchd'; grep -a -nE 'SystemOS|cache|dyld|launchd|libSystem' /mnt/d/aios/_work/klog-last2.txt | tail -16"
