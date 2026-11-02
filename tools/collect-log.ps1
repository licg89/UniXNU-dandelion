# Collect the firmware log that PayloadBootDxe writes into 'flashinfo'.
#
# The firmware writes a DDNLOG01 block at LOG_PARTITION_OFFSET (30512 sectors
# into 'flashinfo') on every boot and flushes it again on a timer and at
# ExitBootServices.  Reading it needs Android, because this phone has no USB
# device mode of its own.
#
#   .\tools\collect-log.ps1
#   .\tools\collect-log.ps1 -Serial QS55XCPRCA9LDA5T -Out out\fw-2026-11-02.txt
#
# Prerequisites: adb on PATH and the phone booted into Android with root
# (`adb shell su -c id` must print uid=0).  A machine with emulators attached
# has more than one adb device, so pass -Serial unless only one phone is
# connected.
param(
    [string]$Serial = "",
    [string]$Partition = "flashinfo",
    [int]$Lba = 30512,
    [int]$Sectors = 128,
    [string]$Out = "out\firmware-log.txt"
)

$ErrorActionPreference = "Stop"

$adbArgs = @()
if ($Serial) { $adbArgs += @("-s", $Serial) }

& adb @adbArgs get-state 2>$null | Out-Null
if ($LASTEXITCODE -ne 0) {
    throw "no adb device; boot Android first (fastboot flash boot out\boot_backup.img) and pass -Serial if several are attached"
}

$root = & adb @adbArgs shell "su -c id" 2>&1
if ($root -notmatch "uid=0") {
    throw "no root through adb: '$root'"
}

$remote = "/data/local/tmp/ddnlog.bin"
Write-Host "dd /dev/block/by-name/$Partition skip=$Lba count=$Sectors on the device"
& adb @adbArgs shell "su -c 'dd if=/dev/block/by-name/$Partition of=$remote bs=512 skip=$Lba count=$Sectors'"
if ($LASTEXITCODE -ne 0) { throw "dd failed" }

$dir = Split-Path -Parent $Out
if ($dir) { New-Item -ItemType Directory -Force $dir | Out-Null }
$local = Join-Path $dir "ddnlog-block.bin"
Write-Host "pull $remote -> $local"
& adb @adbArgs pull $remote $local
if ($LASTEXITCODE -ne 0) { throw "adb pull failed" }

Write-Host "decode -> $Out"
& py -3.13 tools\ddnlog.py $local --out $Out
if ($LASTEXITCODE -ne 0) { throw "decode failed (no DDNLOG01 in the block?)" }

Write-Host ""
Write-Host "--- $Out (tail) ---"
Get-Content $Out -Tail 30

