; Ring-3 isolation test A: writes a marker value into its own address space,
; then exits. Paired with ring3_isolate_b.asm — the kernel launches both as
; separate processes and checks each retains its own value at the SAME
; virtual address, proving per-process address spaces are truly isolated.
; See docs/superpowers/specs/2026-07-20-per-process-address-spaces-design.md
BITS 64
SECTION .text
GLOBAL _start
_start:
    mov rax, 0x8000000800   ; test data address (must match kernel.c's check)
    mov dword [rax], 0xAAAAAAAA
    xor edi, edi             ; arg1 (exit code) = 0
    mov eax, 1                ; syscall 1 = SYS_EXIT
    int 0x80
    hlt
