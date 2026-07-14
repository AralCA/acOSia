; ============================================================
;  acOSia - Kernel + interactive shell (16-bit real mode)
;  ------------------------------------------------------------
;  Loaded at physical address 0x8000 by the stage-1 bootloader.
;  Provides screen output, keyboard input, a line editor, and a
;  small command shell. I/O goes through BIOS services
;  (int 0x10 / int 0x16) -- there is no OS beneath us.
; ============================================================
bits 16
org 0x8000

MAX_INPUT      equ 128
KEY_ENTER      equ 0x0D
KEY_BACKSPACE  equ 0x08

; ------------------------------------------------------------
;  Entry point
; ------------------------------------------------------------
kernel_start:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    cld

    call clear_screen
    mov si, banner
    call print

shell_loop:
    mov si, prompt
    call print

    mov di, input_buffer
    call read_line              ; fills input_buffer, null-terminated

    mov si, input_buffer
    call handle_command
    jmp shell_loop

; ------------------------------------------------------------
;  read_line - read one line into buffer at DI (null-terminated).
;  Handles printable chars, Backspace, and Enter. Echoes input.
; ------------------------------------------------------------
read_line:
    xor cx, cx                  ; cx = current length
.loop:
    xor ah, ah
    int 0x16                    ; wait for key -> AL=ASCII, AH=scancode

    cmp al, KEY_ENTER
    je .done
    cmp al, KEY_BACKSPACE
    je .backspace
    cmp al, 0x20                ; ignore control chars
    jb .loop
    cmp cx, MAX_INPUT-1         ; buffer full?
    jae .loop

    mov [di], al                ; store + echo
    inc di
    inc cx
    mov ah, 0x0E
    int 0x10
    jmp .loop

.backspace:
    test cx, cx
    jz .loop                    ; nothing to delete
    dec di
    dec cx
    mov ah, 0x0E                ; erase on screen: BS, space, BS
    mov al, KEY_BACKSPACE
    int 0x10
    mov al, ' '
    int 0x10
    mov al, KEY_BACKSPACE
    int 0x10
    jmp .loop

.done:
    mov byte [di], 0            ; null-terminate
    call print_newline
    ret

; ------------------------------------------------------------
;  handle_command - dispatch the command in buffer at DS:SI
; ------------------------------------------------------------
handle_command:
    mov al, [si]
    test al, al
    jz .ret                     ; empty line -> new prompt

    mov di, cmd_help
    call streq
    jc do_help
    mov di, cmd_clear
    call streq
    jc do_clear
    mov di, cmd_about
    call streq
    jc do_about
    mov di, cmd_ver
    call streq
    jc do_about
    mov di, cmd_reboot
    call streq
    jc do_reboot

    call try_echo               ; handles "echo" / "echo X"
    jc .ret

    mov si, msg_unknown         ; nothing matched
    call print
    mov si, input_buffer
    call print
    call print_newline
.ret:
    ret

do_help:
    mov si, help_text
    call print
    ret
do_clear:
    call clear_screen
    ret
do_about:
    mov si, about_text
    call print
    ret
do_reboot:
    mov si, msg_reboot
    call print
    jmp 0xFFFF:0x0000           ; jump to reset vector -> reboot

; ------------------------------------------------------------
;  try_echo - if buffer is "echo" or "echo <text>", print it.
;  Returns CF=1 if handled, CF=0 otherwise. Preserves SI.
; ------------------------------------------------------------
try_echo:
    push si
    cmp byte [si+0], 'e'
    jne .no
    cmp byte [si+1], 'c'
    jne .no
    cmp byte [si+2], 'h'
    jne .no
    cmp byte [si+3], 'o'
    jne .no
    mov al, [si+4]
    test al, al
    jz .empty                   ; bare "echo"
    cmp al, ' '
    jne .no                     ; e.g. "echox" is not echo
    add si, 5                   ; skip "echo "
    call print
    call print_newline
    jmp .yes
.empty:
    call print_newline
.yes:
    pop si
    stc
    ret
.no:
    pop si
    clc
    ret

; ------------------------------------------------------------
;  streq - compare null-terminated strings DS:SI and DS:DI.
;  Returns CF=1 if equal, CF=0 otherwise. Preserves SI and DI.
; ------------------------------------------------------------
streq:
    push si
    push di
.next:
    mov al, [si]
    mov bl, [di]
    cmp al, bl
    jne .noteq
    test al, al
    jz .eq
    inc si
    inc di
    jmp .next
.eq:
    pop di
    pop si
    stc
    ret
.noteq:
    pop di
    pop si
    clc
    ret

; ------------------------------------------------------------
;  Low-level output (BIOS teletype, int 0x10 / AH=0Eh)
; ------------------------------------------------------------
print:                          ; DS:SI -> null-terminated string
    push ax
    cld
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

print_newline:
    push ax
    mov ah, 0x0E
    mov al, 13
    int 0x10
    mov al, 10
    int 0x10
    pop ax
    ret

clear_screen:
    push ax
    mov ax, 0x0003              ; set 80x25 text mode -> clears the screen
    int 0x10
    pop ax
    ret

; ------------------------------------------------------------
;  Data
; ------------------------------------------------------------
banner:
    db 13, 10
    db "========================================", 13, 10
    db "           a c O S i a   v0.1", 13, 10
    db "   a tiny 16-bit OS written in assembly", 13, 10
    db "========================================", 13, 10
    db "Type 'help' for a list of commands.", 13, 10, 13, 10, 0

prompt      db "acOSia> ", 0

help_text:
    db "Commands:", 13, 10
    db "  help     show this help", 13, 10
    db "  clear    clear the screen", 13, 10
    db "  echo X   print the text X", 13, 10
    db "  about    what is acOSia", 13, 10
    db "  ver      version information", 13, 10
    db "  reboot   restart the machine", 13, 10, 0

about_text:
    db "acOSia v0.1", 13, 10
    db "A minimal operating system that boots on bare metal,", 13, 10
    db "loads its own kernel from disk, and runs this shell.", 13, 10
    db "Written from scratch in x86 assembly. No OS beneath it.", 13, 10, 0

msg_unknown db "Unknown command: ", 0
msg_reboot  db "Rebooting...", 13, 10, 0

cmd_help    db "help", 0
cmd_clear   db "clear", 0
cmd_about   db "about", 0
cmd_ver     db "ver", 0
cmd_reboot  db "reboot", 0

input_buffer: times MAX_INPUT db 0
