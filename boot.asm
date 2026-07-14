; ============================================================
;  acOSia - Stage 1 bootloader (512-byte boot sector)
;  ------------------------------------------------------------
;  The BIOS loads this sector to 0x7C00 and jumps here in
;  16-bit real mode. Our job: load the kernel from the disk
;  and transfer control to it.
; ============================================================
bits 16
org 0x7C00

KERNEL_OFFSET  equ 0x8000        ; load the kernel here
KERNEL_SECTORS equ 15            ; number of 512-byte sectors to read

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00               ; stack grows down from just below us
    cld
    sti

    mov [boot_drive], dl         ; BIOS passes the boot drive in DL

    mov si, msg_load
    call print

    ; --- load KERNEL_SECTORS sectors starting at LBA 1 (CHS sector 2) ---
    mov bx, KERNEL_OFFSET        ; ES:BX = 0x0000:0x8000 destination
    mov di, 3                    ; retry counter (disk reads can be flaky)
.read:
    mov ah, 0x02                 ; BIOS: read sectors
    mov al, KERNEL_SECTORS
    mov ch, 0                    ; cylinder 0
    mov cl, 2                    ; sector 2 (1-based; sector 1 = this bootsector)
    mov dh, 0                    ; head 0
    mov dl, [boot_drive]
    int 0x13
    jnc .loaded                  ; CF=0 -> success

    xor ah, ah                   ; else: reset disk system...
    mov dl, [boot_drive]
    int 0x13
    dec di                       ; ...and retry
    jnz .read

    mov si, msg_err              ; out of retries
    call print
    jmp hang

.loaded:
    mov si, msg_ok
    call print
    jmp 0x0000:KERNEL_OFFSET     ; far jump -> hand control to the kernel

hang:
    hlt
    jmp hang

; --- print null-terminated string at DS:SI via BIOS teletype ---
print:
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

boot_drive db 0
msg_load   db "acOSia: loading kernel...", 13, 10, 0
msg_ok     db "acOSia: starting kernel", 13, 10, 0
msg_err    db "acOSia: disk read error!", 13, 10, 0

times 510-($-$$) db 0            ; pad to 510 bytes
dw 0xAA55                        ; boot signature in the final 2 bytes
