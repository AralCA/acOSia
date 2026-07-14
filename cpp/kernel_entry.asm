; ============================================================
;  acOSia v2 - 32-bit kernel entry stub
;  ------------------------------------------------------------
;  Linked FIRST, so it sits at the kernel's load address (0x10000).
;  The bootloader calls here in 32-bit protected mode; we hand off
;  to the C++ entry point kmain(). (PE/COFF prefixes C symbols with
;  an underscore, hence _kmain.)
; ============================================================
bits 32
global _start
extern _kmain
section .text

_start:
    call _kmain
    cli
.hang:
    hlt
    jmp .hang
