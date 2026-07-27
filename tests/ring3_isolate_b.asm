; Ring-3 isolation test B: writes a DIFFERENT marker value into its own
; address space, then spins forever. See ring3_isolate_a.asm for the full
; explanation (including why it never calls SYS_EXIT) — this file only
; differs by the marker constant.
BITS 64
SECTION .text
GLOBAL _start
_start:
    mov rax, 0x8040001000   ; test data address (must match kernel.c's check)
    mov dword [rax], 0xBBBBBBBB
.spin:
    pause
    jmp .spin
