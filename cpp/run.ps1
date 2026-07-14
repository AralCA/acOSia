# acOSia v2 run script - builds the image, then boots it in QEMU (as a hard disk).
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
& (Join-Path $root 'build.ps1')

$qemu = (Get-Command qemu-system-i386   -ErrorAction SilentlyContinue).Source
if (-not $qemu) { $qemu = (Get-Command qemu-system-x86_64 -ErrorAction SilentlyContinue).Source }
if (-not $qemu) {
    foreach ($p in @(
        "$env:ProgramFiles\qemu\qemu-system-i386.exe",
        "$env:ProgramFiles\qemu\qemu-system-x86_64.exe")) {
        if (Test-Path $p) { $qemu = $p; break }
    }
}
if (-not $qemu) { throw "QEMU not found. Install: winget install SoftwareFreedomConservancy.QEMU" }

$img = Join-Path $root 'build\acosia.img'
Write-Host "Booting acOSia v2 (C++) in QEMU..." -ForegroundColor Green
& $qemu -drive file=$img,format=raw,if=ide
