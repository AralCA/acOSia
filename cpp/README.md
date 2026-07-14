# acOSia (C++ core)

The 32-bit protected-mode C++ kernel that ships alongside the 16-bit assembly OS
in the parent folder. The bootloader is x86 assembly, but the kernel (VGA driver,
keyboard driver, and shell) is freestanding C++. No standard library, no OS
beneath it: the C++ code talks to the hardware directly. It installs an interrupt
descriptor table, remaps the PIC, and runs a 100 Hz timer plus an interrupt-driven
keyboard (see the `uptime` command).

This is the clean core. The C++ feature demo and the factory game are kept in a
separate repo: [AralCA/acOSia-cpp](https://github.com/AralCA/acOSia-cpp).

## What is different from the assembly version

| | assembly (`../`) | C++ core (this folder) |
|---|---|---|
| CPU mode | 16-bit real mode | 32-bit protected mode |
| Kernel language | assembly | freestanding C++ |
| Screen output | BIOS `int 0x10` | own VGA driver (writes `0xB8000`) |
| Keyboard | BIOS `int 0x16` | own PS/2 driver, IRQ1 (interrupt-driven) |

In protected mode the BIOS interrupt services are gone, so the C++ kernel
implements its own drivers, which is what makes it interesting.

## Build and run

Needs NASM, QEMU, and the mingw-w64 `g++`/`ld`/`objcopy`.

```powershell
./run.ps1      # builds and boots in QEMU (as a hard disk)
./build.ps1    # build only, produces build/acosia.img
```

## How it works

```
BIOS -> boot.asm @ 0x7C00 (16-bit real mode)
          |  1. load the kernel from disk (int 13h AH=42h, LBA read)
          |  2. enable the A20 line
          |  3. load a flat GDT, set CR0.PE, far-jump to 32-bit code
          v
       kernel_entry.asm @ 0x10000 (32-bit)
          |  call _kmain
          v
       kernel.cpp: Vga + Keyboard + Shell classes
          VGA text at 0xB8000, PS/2 keyboard at 0x60/0x64
```

## Build pipeline

Bare-metal C++ on a Windows/mingw toolchain has a few sharp edges, all handled in
`build.ps1`:

1. `g++ -m32 -ffreestanding` compiles the kernel with no standard library.
2. SSE, MMX, and x87 are disabled (`-mno-sse -mno-sse2 -mno-mmx -mno-80387`). At
   `-O2` the compiler otherwise auto-vectorizes loops into SSE instructions, which
   fault (`#UD`) because SSE is off (`CR4=0`). With no IDT set up, that fault
   triple-faults the machine. Disabling SSE keeps codegen to general-purpose
   registers.
3. `memcpy` and `memset` are provided in the kernel, since the compiler may emit
   calls to them and there is no libc here to supply them.
4. mingw's `ld` only emits PE, so we link a 32-bit PE with `.text` based at
   `0x10000`, then `objcopy -O binary` flattens the real sections into a raw
   kernel the bootloader can load and jump straight into.

## Files

```
boot.asm          bootloader: disk load + protected-mode switch + GDT
kernel_entry.asm  32-bit stub that calls into C++
kernel.cpp        VGA driver, PS/2 keyboard driver, shell
build.ps1         assemble + compile + link + flatten + pack image
run.ps1           build, then boot in QEMU
```
