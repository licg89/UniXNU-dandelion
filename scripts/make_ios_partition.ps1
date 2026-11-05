# Give the iOS data area its own GPT partition, so Android keeps working between tests.
#
# Run with the phone booted into Android and USB debugging authorised:
#   powershell -ExecutionPolicy Bypass -File D:\aios\_work\make_ios_partition.ps1
#
# It dumps the primary GPT, carves a 30 GiB `ios` partition out of the tail of userdata,
# writes both GPT copies back, reformats the (now smaller) userdata and flashes the iOS
# image into `ios`.  After this, an iOS test is only `fastboot flash boot` + reboot, and
# Android - including developer options and the USB debugging grant - stays as it is.

param(
  [double] $IosGiB = 30,
  [string] $Serial = 'QS55XCPRCA9LDA5T',
  [string] $Image = 'D:\aios\out3\userdata-signed.img'
)

$ErrorActionPreference = 'Continue'
$work = 'D:\aios\out3\gpt'

Write-Host '== root'
adb -s $Serial root | Out-Null
Start-Sleep -Seconds 3
adb -s $Serial shell id

Write-Host '== dumping the primary GPT (sectors 0..33)'
adb -s $Serial shell 'dd if=/dev/block/mmcblk0 bs=512 count=34 of=/data/local/tmp/gpt.bin 2>/dev/null'
adb -s $Serial pull /data/local/tmp/gpt.bin "$work-dump.bin" 2>&1 | Select-Object -Last 1

Write-Host '== editing the partition table'
wsl -d Ubuntu -e bash -lc "sed -i 's/\r$//' /mnt/d/aios/_work/make_ios_partition.py; python3 /mnt/d/aios/_work/make_ios_partition.py $(wslpath -a $work-dump.bin) $(wslpath -a $work) --ios-gib $IosGiB"

if (-not (Test-Path "$work-primary.bin")) { Write-Host 'the edit failed'; exit 1 }

Write-Host '== writing the primary and backup copies back'
adb -s $Serial push "$work-primary.bin" /data/local/tmp/gpt-primary.bin 2>&1 | Select-Object -Last 1
adb -s $Serial push "$work-backup.bin"  /data/local/tmp/gpt-backup.bin  2>&1 | Select-Object -Last 1

$lastLba = adb -s $Serial shell 'cat /sys/block/mmcblk0/size'
$lastLba = [int64]$lastLba.Trim() - 1
Write-Host "disk last LBA $lastLba"

adb -s $Serial shell "dd if=/data/local/tmp/gpt-primary.bin of=/dev/block/mmcblk0 bs=512 seek=0 conv=notrunc 2>/dev/null"
adb -s $Serial shell "dd if=/data/local/tmp/gpt-backup.bin of=/dev/block/mmcblk0 bs=512 seek=$($lastLba - 32) conv=notrunc 2>/dev/null"

Write-Host '== the new table as Android sees it'
adb -s $Serial shell 'ls -l /dev/block/by-name/ | grep -E "userdata|ios"'

Write-Host '== reformatting the smaller userdata'
adb -s $Serial reboot bootloader | Out-Null
Start-Sleep -Seconds 15
fastboot devices
fastboot -w 2>&1 | Select-String 'format successful|Finished'

Write-Host '== flashing the iOS image into the ios partition'
fastboot flash ios $Image 2>&1 | Select-String 'Finished|FAILED'

Write-Host '== done; boot Android once to confirm it still works'
fastboot reboot
