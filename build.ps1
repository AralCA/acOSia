# acOSia build script
# Assembles the bootloader + kernel and packs them into a bootable disk image.
$ErrorActionPreference = 'Stop'
$root  = $PSScriptRoot
$build = Join-Path $root 'build'
New-Item -ItemType Directory -Force -Path $build | Out-Null

Write-Host 'Assembling bootloader...' -ForegroundColor Cyan
nasm -f bin (Join-Path $root 'boot.asm')   -o (Join-Path $build 'boot.bin')
Write-Host 'Assembling kernel...' -ForegroundColor Cyan
nasm -f bin (Join-Path $root 'kernel.asm') -o (Join-Path $build 'kernel.bin')

$KERNEL_SECTORS = 15          # must match KERNEL_SECTORS in boot.asm
$SECTOR         = 512
$IMG_SIZE       = 1474560     # 1.44 MB floppy image

$boot   = [System.IO.File]::ReadAllBytes((Join-Path $build 'boot.bin'))
$kernel = [System.IO.File]::ReadAllBytes((Join-Path $build 'kernel.bin'))

if ($boot.Length -ne $SECTOR) {
    throw "boot.bin must be exactly $SECTOR bytes, got $($boot.Length)."
}
$maxKernel = $KERNEL_SECTORS * $SECTOR
if ($kernel.Length -gt $maxKernel) {
    throw "kernel is $($kernel.Length) bytes, exceeds $maxKernel. Raise KERNEL_SECTORS in boot.asm AND build.ps1."
}

# Lay the image out: [ boot sector | kernel | zero padding ]
$img = New-Object byte[] $IMG_SIZE
[Array]::Copy($boot,   0, $img, 0,       $boot.Length)      # sector 0
[Array]::Copy($kernel, 0, $img, $SECTOR, $kernel.Length)    # sector 1+

$imgPath = Join-Path $build 'acosia.img'
[System.IO.File]::WriteAllBytes($imgPath, $img)
Write-Host "Built $imgPath  (boot=$($boot.Length)B, kernel=$($kernel.Length)B)" -ForegroundColor Green
