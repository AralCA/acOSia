# acOSia v2 build script
# Assembles the bootloader + entry stub, compiles the C++ kernel freestanding,
# links a PE and flattens it to a raw binary, then packs a bootable disk image.
$ErrorActionPreference = 'Stop'
$root  = $PSScriptRoot
$build = Join-Path $root 'build'
New-Item -ItemType Directory -Force -Path $build | Out-Null

Write-Host 'Assembling bootloader...' -ForegroundColor Cyan
nasm -f bin   (Join-Path $root 'boot.asm')         -o (Join-Path $build 'boot.bin')

Write-Host 'Assembling kernel entry stub...' -ForegroundColor Cyan
nasm -f win32 (Join-Path $root 'kernel_entry.asm') -o (Join-Path $build 'entry.o')

Write-Host 'Compiling C++ kernel (freestanding, 32-bit)...' -ForegroundColor Cyan
$cxx = @(
    '-m32','-march=i686','-ffreestanding','-fno-exceptions','-fno-rtti',
    '-mno-mmx','-mno-sse','-mno-sse2','-mno-80387',    # no SSE/MMX/x87: those opcodes fault (CR4=0)
    '-fno-stack-protector','-fno-pic','-fno-builtin',
    '-fno-tree-loop-distribute-patterns','-fno-asynchronous-unwind-tables',
    '-std=c++17','-O2','-Wall','-c'
)
$out = & g++ @cxx (Join-Path $root 'kernel.cpp') -o (Join-Path $build 'kernel.o') 2>&1
if ($out) { $out }
if ($LASTEXITCODE -ne 0) { throw 'C++ compile failed' }

# mingw's ld is PE-only, so we link a 32-bit PE with .text based at 0x10000,
# then objcopy the real sections out to a flat binary. (The "section below
# image base" notes are expected -- PE wants base 0x400000; we load at 0x10000.)
Write-Host 'Linking (PE) and flattening to a raw binary...' -ForegroundColor Cyan
$kpe = Join-Path $build 'kernel.pe'
$out = & ld -m i386pe -o $kpe -Ttext 0x10000 -e _start `
    (Join-Path $build 'entry.o') (Join-Path $build 'kernel.o') 2>&1
if ($LASTEXITCODE -ne 0) { $out; throw 'link failed' }
$out = & objcopy -O binary -j .text -j .rdata -j .data $kpe (Join-Path $build 'kernel.bin') 2>&1
if ($LASTEXITCODE -ne 0) { $out; throw 'objcopy failed' }

$SECTOR = 512; $KERNEL_SECTORS = 64; $IMG_SIZE = 1048576   # 1 MB disk image
$boot = [IO.File]::ReadAllBytes((Join-Path $build 'boot.bin'))
$kern = [IO.File]::ReadAllBytes((Join-Path $build 'kernel.bin'))
if ($boot.Length -ne $SECTOR) { throw "boot.bin must be $SECTOR bytes, got $($boot.Length)" }
$max = $KERNEL_SECTORS * $SECTOR
if ($kern.Length -gt $max) { throw "kernel is $($kern.Length) bytes, exceeds $max; raise KERNEL_SECTORS" }

$img = New-Object byte[] $IMG_SIZE
[Array]::Copy($boot, 0, $img, 0,       $boot.Length)
[Array]::Copy($kern, 0, $img, $SECTOR, $kern.Length)
$imgPath = Join-Path $build 'acosia.img'
[IO.File]::WriteAllBytes($imgPath, $img)
Write-Host "Built $imgPath  (boot=$($boot.Length)B, kernel=$($kern.Length)B)" -ForegroundColor Green
