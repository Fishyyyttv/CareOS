; Ring-3 fault test: null dereference — kernel must survive
BITS 32
SECTION .text
GLOBAL _start
_start:
    xor eax, eax
    mov dword [eax], 42
    mov eax, 1
    xor ebx, ebx
    int 0x80
