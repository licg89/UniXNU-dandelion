# One test round on the phone.
#
# The physical reality this encodes (measured on this board, not guessed):
#
#   * LK cannot `fastboot boot`; every experiment is `fastboot flash boot`.
#   * When our firmware hangs or resets, the phone does NOT come back to
#     fastboot by itself.  Hold power ~10 s to force it off, then Power +
#     Volume Down to get into fastboot.  `wait-fastboot` below says when.
#   * The only log this port produces is the firmware's DDNLOG01 block inside
#     'flashinfo' (offset 30512), and reading it needs Android.
#
# usage:
#   powershell -File tools\round.ps1 flash   -BootImg out\unixnu-build\boot.img
#   powershell -File tools\round.ps1 android -AndroidBoot C:\aios\artifacts\boot-android-lineage.img
#   powershell -File tools\round.ps1 log     -Serial QS55XCPRCA9LDA5T
#   powershell -File tools\round.ps1 status
#
# Everything it writes goes to out\ (gitignored).

param(
    [Parameter(Position = 0)]
    [ValidateSet("flash", "android", "log", "status", "wait-fastboot")]
    [string]$Stage = "status",
    [string]$BootImg = "out\unixnu-build\boot.img",
    [string]$AndroidBoot = "",
    [string]$Serial = "",
    [int]$TimeoutMinutes = 10
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)

function Say($text, $colour = "Gray") { Write-Host $text -ForegroundColor $colour }
function AdbArgs { if ($Serial) { @("-s", $Serial) } else { @() } }

function Test-Fastboot {
    @(& fastboot devices 2>&1 | Where-Object { $_ -match "\s+fastboot$" })
}

function Test-Android {
    @(& adb devices 2>&1 | Where-Object { $_ -match "\s+device$" })
}

function Wait-For($kind, $minutes) {
    $deadline = (Get-Date).AddMinutes($minutes)
    while ((Get-Date) -lt $deadline) {
        if ($kind -eq "fastboot") {
            $d = @(Test-Fastboot); if ($d.Count -gt 0) { return $d[0] }
        } else {
            $d = @(Test-Android); if ($d.Count -gt 0) { return $d[0] }
        }
        Start-Sleep -Seconds 3
    }
    throw "no device in $kind after $minutes minutes"
}

switch ($Stage) {
    "status" {
        Say "adb     : $(@(Test-Android) -join ', ')"
        Say "fastboot: $(@(Test-Fastboot) -join ', ')"
        Say "boot img: $BootImg $(if (Test-Path $BootImg) { '(' + (Get-Item $BootImg).Length + ' bytes)' } else { '(missing)' })"
    }

    "flash" {
        if (-not (Test-Path $BootImg)) { throw "no such image: $BootImg" }
        Say "image: $BootImg ($((Get-Item $BootImg).Length) bytes)"
        if (@(Test-Android).Count -gt 0) {
            Say "rebooting to bootloader"
            $adbArgs = AdbArgs
            & adb @adbArgs reboot bootloader | Out-Null
        }
        $null = Wait-For "fastboot" $TimeoutMinutes
        Say "flashing boot" Yellow
        & fastboot flash boot $BootImg
        if ($LASTEXITCODE -ne 0) { throw "fastboot flash failed" }
        Say "rebooting into the chain -- watch the panel now" Yellow
        & fastboot reboot | Out-Null
    }

    "android" {
        if (-not $AndroidBoot) { throw "pass -AndroidBoot <image>" }
        if (-not (Test-Path $AndroidBoot)) { throw "no such image: $AndroidBoot" }
        if (@(Test-Fastboot).Count -eq 0) {
            Say "not in fastboot: hold power ~10 s, then Power + Volume Down" Yellow
            $null = Wait-For "fastboot" $TimeoutMinutes
        }
        Say "flashing Android boot: $AndroidBoot" Yellow
        & fastboot flash boot $AndroidBoot
        if ($LASTEXITCODE -ne 0) { throw "fastboot flash failed" }
        & fastboot reboot | Out-Null
        $null = Wait-For "android" $TimeoutMinutes
        Say "Android is up" Green
    }

    "log" {
        & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $repo "tools\collect-log.ps1") `
            -Serial $Serial -Out (Join-Path $repo "out\firmware-log.txt")
    }

    "wait-fastboot" {
        if (@(Test-Fastboot).Count -eq 0) {
            Say "hold power ~10 s, then Power + Volume Down" Yellow
        }
        $d = Wait-For "fastboot" $TimeoutMinutes
        Say "fastboot: $d" Green
    }
}
