# Unattended boot-and-collect for this board.
#
# The difference from collect-slide.ps1: this one never writes the boot partition.  It puts the Android
# boot image back once, starts the test image with `fastboot boot` (temporary, RAM only), lets the image's
# own dandelion-autoreset timer reset the board, and then reads the console block from Android.  Nothing
# needs a key press, and a wrong image cannot leave the phone in a state that needs a manual recovery.
#
# The image must carry dandelion-autoreset=<seconds>; its own thread resets the board with PSCI
# SYSTEM_RESET, the same conduit ArmDTPlatform already uses.
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File scripts\collect-unattended.ps1 `
#       -Image D:\aios\out3\boot-sr54-auto.img -Tag sr54 -AutoresetSeconds 180

param(
    [Parameter(Mandatory = $true)][string]$Image,
    [Parameter(Mandatory = $true)][string]$Tag,
    [int]$AutoresetSeconds = 180,
    [string]$Serial = 'QS55XCPRCA9LDA5T',
    [string]$AndroidBoot = 'D:\aios\artifacts\boot_backup.img',
    [int]$MarginSeconds = 90,
    [string]$LogDir = 'D:\aios\_work\UniXNU-dandelion\out'
)

$ErrorActionPreference = 'Continue'
$flashinfo = '/dev/block/by-name/flashinfo'

function Say($m) { Write-Host "[$(Get-Date -Format 'HH:mm:ss')] $m" }

function Wait-Fastboot([int]$minutes) {
    $deadline = (Get-Date).AddMinutes($minutes)
    while ((Get-Date) -lt $deadline) {
        if (@(fastboot devices 2>$null | Where-Object { $_ -match 'fastboot' }).Count -gt 0) { return $true }
        Start-Sleep -Seconds 3
    }
    return $false
}

function Wait-Android([int]$minutes) {
    $deadline = (Get-Date).AddMinutes($minutes)
    while ((Get-Date) -lt $deadline) {
        $d = @(adb devices 2>$null | Where-Object { $_ -match "\d\s+device$" })
        if ($d.Count -gt 0) { return $true }
        Start-Sleep -Seconds 3
    }
    return $false
}

if (-not (Test-Path $Image)) { throw "image not found: $Image" }
$sha = (Get-FileHash $Image -Algorithm SHA256).Hash.ToLower()
Say "image $Image sha256 $sha"

Say 'waiting for fastboot (the phone must already be in the bootloader)'
if (-not (Wait-Fastboot 30)) { throw 'fastboot did not appear' }

# One-time restore of the Android boot image, so the reset after the test lands in Android instead of
# booting the previous UniXNU image again.
if (Test-Path $AndroidBoot) {
    Say 'restoring the Android boot image so the post-test reset returns to Android'
    fastboot flash boot $AndroidBoot 2>&1 | Select-String 'Writing|Finished|FAILED'
}

Say "booting the test image with fastboot boot (no partition is written)"
$boot = fastboot boot $Image 2>&1
$boot | Select-String 'Sending|Booting|Finished|FAILED|error'
if ($LASTEXITCODE -ne 0) { Say 'fastboot boot refused; stopping'; exit 1 }

$wait = $AutoresetSeconds + $MarginSeconds
Say "waiting up to $wait s for the image to reset itself, then for Android"
if (-not (Wait-Android ([math]::Ceiling($wait / 60) + 10))) {
    Say 'Android did not appear; the autoreset may not have fired'
    Say 'hold power ~10 s, then Power + Volume Down, and read the log by hand'
    exit 1
}

Say 'Android is up; reading the console block'
$raw = Join-Path $env:TEMP "fi-$Tag.bin"
adb -s $Serial shell su -c "dd if=$flashinfo of=/data/local/tmp/fi.bin bs=1M" 2>&1 | Out-Null
adb -s $Serial pull /data/local/tmp/fi.bin $raw 2>&1 | Select-String 'pulled'
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$bin = Join-Path $env:TEMP "flashinfo-$Tag-$stamp.bin"
Move-Item -Force $raw $bin
Say "console block: $bin"

Say 'clearing the console window so the next boot starts empty'
adb -s $Serial shell su -c "dd if=/dev/zero of=$flashinfo bs=1M count=1" 2>&1 | Out-Null

Say "done. tag=$Tag image=$sha"
Say "read it with: powershell -File scripts\collect-slide.ps1 -Tag $Tag (it will symbolize the traps)"
