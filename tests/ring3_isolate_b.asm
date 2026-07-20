; Ring-3 isolation test B: writes a DIFFERENT marker value into its own
; address space, then exits. See ring3_isolate_a.asm for the full
; explanation — this file only differs by the marker constant.
BITS 64
SECTION .text
GLOBAL _start
_start:
    mov rax, 0x8000000800   ; test data address (must match kernel.c's check)
    mov dword [rax], 0xBBBBBBBB
    xor edi, edi             ; arg1 (exit code) = 0
    mov eax, 1                ; syscall 1 = SYS_EXIT
    int 0x80
    hlt
