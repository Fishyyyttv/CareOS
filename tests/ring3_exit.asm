; Ring-3 smoke test: calls exit(0) via int 0x80
BITS 64
SECTION .text
GLOBAL _start
_start:
    xor edi, edi    ; arg1 (exit code) = 0
    mov eax, 1      ; syscall 1 = SYS_EXIT
    int 0x80
    hlt
