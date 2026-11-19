# One unattended round: flash the test image, let the board return by itself, read the log, restore Android.
#
# The board returns on its own now: dandelion-autoreset writes MediaTek's reboot-mode register (0x10007000
# + 0x24, low four bits = 0x03 for the bootloader) and then the software-reset key (0x10007000 + 0x14,
# 0x1209), so it comes back into fastboot without a key press.  That is what makes this script possible;
# before it, every round ended with somebody holding the power button.
#
# The loop is: flash test image -> wait for the board to reset into fastboot -> flash the Android boot
# partition -> wait for adb -> pull and decode the console block -> put the board back in fastboot.
# On any failure the Android boot image is restored first, so the phone is never left unbootable.
#
# fastboot and adb write their progress to stderr, and PowerShell turns that into terminating errors under
# 'Stop', so external calls are run with 'Continue' and judged by $LASTEXITCODE instead.
param(
  [Parameter(Mandatory=$true)][string]$Image,
  [string]$Tag = 'round',
  [int]$AutoresetSeconds = 240,
  # dandelion-autoreset counts from the moment the display provider starts, not from power-on, so the wall
  # time to the reset is AutoresetSeconds plus however long the boot takes to reach the display (about five
  # minutes on this board).  The slack covers that plus the reset and the bootloader's own start-up.
  [int]$ReturnSlackSeconds = 600,
  [switch]$StayInFastboot,
  [string]$Serial = 'QS55XCPRCA9LDA5T'
)

if ($PSVersionTable.PSVersion.Major -ge 7) { $PSNativeCommandUseErrorActionPreference = $false }
$repo = Split-Path -Parent $PSScriptRoot
$android = 'D:\aios\artifacts\boot_backup.img'
$raw = "D:\aios\flashinfo-$Tag-$(Get-Date -Format 'yyyyMMdd-HHmmss').bin"

function Say([string]$m) { Write-Host ("[{0}] {1}" -f (Get-Date -Format 'HH:mm:ss'), $m) }

function Run([scriptblock]$block, [string]$what) {
  $ErrorActionPreference = 'Continue'
  $out = & $block 2>&1 | Out-String
  $code = $LASTEXITCODE
  $ErrorActionPreference = 'Stop'
  if ($out.Trim()) { $out.Trim().Split("`n") | ForEach-Object { Say ("  " + $_.Trim()) } }
  if ($code -ne 0) { throw "$what failed with exit code $code" }
  return $out
}

function WaitFastboot([int]$seconds) {
  $deadline = (Get-Date).AddSeconds($seconds)
  while ((Get-Date) -lt $deadline) {
    $ErrorActionPreference = 'Continue'
    $n = @(fastboot devices 2>$null).Count
    $ErrorActionPreference = 'Stop'
    if ($n -gt 0) { return $true }
    Start-Sleep -Seconds 3
  }
  return $false
}

function WaitAndroid([int]$seconds) {
  # adb answering is not enough: the console block is read through the block device, which needs root, and
  # that is only available once the system has finished booting.
  $deadline = (Get-Date).AddSeconds($seconds)
  while ((Get-Date) -lt $deadline) {
    $ErrorActionPreference = 'Continue'
    $d = @(adb devices 2>$null | Select-Object -Skip 1 | Where-Object { $_ -match '\sdevice$' })
    if ($d.Count -gt 0) {
      $done = (adb -s $Serial shell getprop sys.boot_completed 2>$null | Out-String).Trim()
      if ($done -eq '1') { $ErrorActionPreference = 'Stop'; return $true }
    }
    $ErrorActionPreference = 'Stop'
    Start-Sleep -Seconds 5
  }
  return $false
}

if (-not (Test-Path $Image)) { throw "image not found: $Image" }
Say "image $Image sha256 $((Get-FileHash $Image -Algorithm SHA256).Hash.ToLower())"

# Start from wherever the phone is: Android can be asked to reboot into the bootloader, so a round can be
# begun without touching the phone.
if (-not (WaitFastboot 5)) {
  $ErrorActionPreference = 'Continue'
  $inAndroid = @(adb devices 2>$null | Select-Object -Skip 1 | Where-Object { $_ -match '\sdevice$' }).Count -gt 0
  if ($inAndroid) {
    Say 'phone is in Android; asking it to reboot into the bootloader'
    adb -s $Serial reboot bootloader 2>&1 | Out-Null
  }
  $ErrorActionPreference = 'Stop'
  if (-not (WaitFastboot 120)) { throw 'the phone is not in fastboot and could not be sent there' }
}
Say 'flashing the test image'
Run { fastboot flash boot $Image } 'flash test image' | Out-Null
Run { fastboot reboot } 'reboot' | Out-Null

$wait = $AutoresetSeconds + $ReturnSlackSeconds   # see the note on ReturnSlackSeconds
Say "waiting up to $wait s for the board to reset into fastboot by itself"
if (-not (WaitFastboot $wait)) {
  Say 'the board did not return by itself; restoring Android and stopping'
  $ErrorActionPreference = 'Continue'
  fastboot flash boot $android 2>&1 | Out-Null
  fastboot reboot 2>&1 | Out-Null
  throw 'no automatic return'
}
Say 'the board returned to fastboot by itself'

Say 'flashing the Android boot image to read the log'
Run { fastboot flash boot $android } 'flash android boot' | Out-Null
Run { fastboot reboot } 'reboot to android' | Out-Null
if (-not (WaitAndroid 300)) { throw 'Android did not come up' }

Say "pulling the console block -> $raw"
$ErrorActionPreference = 'Continue'
adb -s $Serial root 2>&1 | Out-Null
Start-Sleep -Seconds 3
adb -s $Serial wait-for-device 2>&1 | Out-Null
$ErrorActionPreference = 'Stop'
$dumped = $false
for ($try = 1; $try -le 6 -and -not $dumped; $try++) {
  $ErrorActionPreference = 'Continue'
  adb -s $Serial shell 'rm -f /data/local/tmp/fi.bin; dd if=/dev/block/by-name/flashinfo of=/data/local/tmp/fi.bin bs=1M' 2>&1 | Out-Null
  $size = (adb -s $Serial shell 'stat -c %s /data/local/tmp/fi.bin 2>/dev/null' 2>$null | Out-String).Trim()
  $ErrorActionPreference = 'Stop'
  if ($size -match '^\d+$' -and [int64]$size -gt 1000000) { $dumped = $true; Say "  dumped $size bytes" }
  else { Say "  attempt $try did not produce a full block ($size); retrying"; Start-Sleep -Seconds 8 }
}
if (-not $dumped) { throw 'could not dump the flashinfo partition' }
Run { adb -s $Serial pull /data/local/tmp/fi.bin $raw } 'pull flashinfo' | Out-Null
Run { adb -s $Serial shell 'dd if=/dev/zero of=/dev/block/by-name/flashinfo bs=512 count=28800 conv=notrunc 2>/dev/null' } 'clear flashinfo' | Out-Null
Say "saved $((Get-Item $raw).Length) bytes"

if ($StayInFastboot) {
  Say 'returning to fastboot for the next round'
  Run { adb -s $Serial reboot bootloader } 'reboot bootloader' | Out-Null
  if (-not (WaitFastboot 120)) { throw 'could not return to fastboot' }
  Say 'ready for the next round'
} else {
  Say 'leaving the phone in Android'
}
Say "done; log at $raw"
