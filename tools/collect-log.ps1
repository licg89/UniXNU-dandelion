# Collect the firmware log that PayloadBootDxe writes into 'flashinfo'.
#
# The firmware writes a DDNLOG01 block at LOG_PARTITION_OFFSET (30512 sectors
# into 'flashinfo') on every boot and flushes it again on a timer and at
# ExitBootServices.  Reading it needs Android, because this phone has no USB
# device mode of its own: boot Android (boot_backup.img), then run this.
#
#   .\tools\collect-log.ps1
#   .\tools\collect-log.ps1 -Out out\fw-2026-11-02.txt
#
# Prerequisites: adb on PATH, the phone in Android with adb root (LineageOS
# allows `adb root`; without root /dev/block/* is refused).
param(
    [string]$Partition = "/dev/block/by-name/flashinfo",
    [int]$Lba = 30512,
    [int]$Sectors = 128,
    [string]$Out = "out\firmware-log.txt"
)

$ErrorActionPreference = "Stop"

& adb get-state 2>$null | Out-Null
if ($LASTEXITCODE -ne 0) {
    throw "no adb device; boot Android first (fastboot flash boot out\boot_backup.img)"
}

$remote = "/data/local/tmp/ddnlog.bin"
Write-Host "dd $Partition skip=$Lba count=$Sectors on the device"
& adb shell "su -c 'dd if=$Partition of=$remote bs=512 skip=$Lba count=$Sectors'"
if ($LASTEXITCODE -ne 0) { throw "dd failed (is adb root available?)" }

New-Item -ItemType Directory -Force (Split-Path -Parent $Out) | Out-Null
$local = Join-Path (Split-Path -Parent $Out) "ddnlog-block.bin"
Write-Host "pull $remote"
& adb pull $remote $local
if ($LASTEXITCODE -ne 0) { throw "adb pull failed" }

Write-Host "decode $local -> $Out"
& py -3.13 tools\ddnlog.py $local --out $Out
if ($LASTEXITCODE -ne 0) { throw "decode failed (no DDNLOG01 in the block?)" }

Write-Host ""
Write-Host "--- $Out (tail) ---"
Get-Content $Out -Tail 30
