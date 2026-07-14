; ============================================================
;  acOSia v2 - Stage 1 bootloader
;  ------------------------------------------------------------
;  BIOS loads this at 0x7C00 in 16-bit real mode. We load the
;  32-bit C++ kernel from disk, enable A20, set up a flat GDT,
;  switch the CPU to protected mode, and jump into the kernel.
; ============================================================
bits 16
org 0x7C00

KERNEL_SEG     equ 0x1000        ; load kernel to 0x1000:0x0000 = phys 0x10000
KERNEL_PHYS    equ 0x10000
KERNEL_LBA     equ 1             ; kernel begins at LBA 1 (right after boot sector)
KERNEL_SECTORS equ 64            ; 32 KB of headroom

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    cld
    mov [boot_drive], dl         ; BIOS gives us the boot drive in DL

    mov si, msg_load
    call print16

    ; --- load the kernel with an LBA read (int 13h AH=42h) ---
    mov si, dap
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jc disk_error

    ; --- enable the A20 line (fast gate via port 0x92) ---
    in al, 0x92
    or al, 2
    out 0x92, al

    ; --- enter 32-bit protected mode ---
    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1                    ; set the PE (Protection Enable) bit
    mov cr0, eax
    jmp CODE_SEG:init_pm         ; far jump flushes the pipeline, loads CS

disk_error:
    mov si, msg_err
    call print16
    jmp $

; --- 16-bit BIOS teletype print (DS:SI -> null-terminated) ---
print16:
    push ax
.next:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0E
    int 0x10
    jmp .next
.done:
    pop ax
    ret

; --- Disk Address Packet used by the LBA read ---
dap:
    db 0x10                      ; packet size
    db 0                         ; reserved
    dw KERNEL_SECTORS            ; number of sectors to read
    dw 0x0000                    ; destination offset
    dw KERNEL_SEG                ; destination segment
    dq KERNEL_LBA                ; starting LBA

; --- Global Descriptor Table: flat 4 GB code + data ---
gdt_start:
    dq 0x0000000000000000        ; null descriptor
gdt_code:                        ; base=0, limit=0xFFFFF, 4KB granularity, 32-bit
    dw 0xFFFF, 0x0000
    db 0x00, 10011010b, 11001111b, 0x00
gdt_data:                        ; same, data segment
    dw 0xFFFF, 0x0000
    db 0x00, 10010010b, 11001111b, 0x00
gdt_end:
gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start
CODE_SEG equ gdt_code - gdt_start
DATA_SEG equ gdt_data - gdt_start

; --- 32-bit entry: set segments, stack, and call into the kernel ---
bits 32
init_pm:
    mov ax, DATA_SEG
    mov ds, ax
    mov ss, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov esp, 0x90000             ; a comfortable stack in free memory
    call KERNEL_PHYS             ; -> kernel_entry -> kmain()
    cli
.hang:
    hlt
    jmp .hang

boot_drive db 0
msg_load db "acOSia: loading C++ kernel...", 13, 10, 0
msg_err  db "acOSia: disk error!", 13, 10, 0

times 510-($-$$) db 0
dw 0xAA55
