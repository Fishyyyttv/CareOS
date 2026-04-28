; =============================================================================
; CareOS boot/boot.asm -- Multiboot2 to x86_64 Long Mode transition
; =============================================================================
BITS 32

; -- Multiboot2 header -----------------------------------------------------
MB2_MAGIC     equ 0xE85250D6
MB2_ARCH      equ 0           ; i386 protected mode (required for MB2 entry)
MB2_HDR_LEN   equ (mb2_end - mb2_start)
MB2_CHECKSUM  equ -(MB2_MAGIC + MB2_ARCH + MB2_HDR_LEN)

section .multiboot2
align 8
mb2_start:
    dd MB2_MAGIC
    dd MB2_ARCH
    dd MB2_HDR_LEN
    dd MB2_CHECKSUM

    ; -- Framebuffer tag (type=5) ------------------------------------------
    align 8
    dw 5            ; type: framebuffer
    dw 1            ; flags: 1 = optional
    dd 20           ; size
    dd 1920         ; width
    dd 1080         ; height
    dd 32           ; depth

    ; -- End tag -----------------------------------------------------------
    align 8
    dw 0
    dw 0
    dd 8
mb2_end:

; -- Page Tables (Temporary) -----------------------------------------------
section .bss
align 4096
pml4:
    resb 4096
pdpt:
    resb 4096
pd:
    resb 4096
stack_bottom:
    resb 65536
stack_top:

; -- Entry -----------------------------------------------------------------
section .text
global _start
extern kernel_main

_start:
    cli
    mov esp, stack_top
    
    ; Save multiboot info
    push eax ; magic
    push ebx ; pointer

    ; 1. Check for Long Mode support (CPUID)
    ; (Omitting full checks for brevity, assuming modern x86_64 for now)
    
    ; 2. Build Page Tables
    ; Identity map the first 2MB (0-2MB)
    ; PML4[0] -> PDPT
    ; PDPT[0] -> PD
    ; PD[0] -> 0 (huge page 2MB)
    
    mov edi, pml4
    mov cr3, edi
    xor eax, eax
    mov ecx, 4096/4
    rep stosd       ; Clear PML4
    
    mov edi, pdpt
    xor eax, eax
    mov ecx, 4096/4
    rep stosd       ; Clear PDPT
    
    mov edi, pd
    xor eax, eax
    mov ecx, 4096/4
    rep stosd       ; Clear PD
    
    ; PML4[0] = pdpt | 3 (Present + Write)
    mov eax, pdpt
    or eax, 0b11
    mov [pml4], eax
    
    ; PDPT[0] = pd | 3 (Present + Write)
    mov eax, pd
    or eax, 0b11
    mov [pdpt], eax
    
    ; PD[0...511] = identity map 1GB using 2MB huge pages
    mov ecx, 0
.map_pd:
    mov eax, 0x200000
    mul ecx
    or eax, 0x83 ; Present + Write + Huge
    mov [pd + ecx * 8], eax
    mov dword [pd + ecx * 8 + 4], 0 ; Clear upper 32 bits
    inc ecx
    cmp ecx, 512
    jne .map_pd
    
    ; 3. Enable PAE
    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax
    
    ; 4. Set EFER.LME (Long Mode Enable)
    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8
    wrmsr
    
    ; 5. Enable Paging
    mov eax, cr0
    or eax, 1 << 31
    mov cr0, eax
    
    ; 6. Load 64-bit GDT
    lgdt [gdt64.ptr]
    
    ; 7. Far Jump to 64-bit code
    pop ebx ; Restore multiboot info pointer
    pop eax ; Restore magic
    mov edi, eax
    mov esi, ebx
    jmp gdt64.code:_start64

BITS 64
_start64:
    ; Set 64-bit stack pointer explicitly
    mov rsp, stack_top

    ; Set up 64-bit segment registers
    mov ax, gdt64.data
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Arguments already in rdi (magic) and rsi (mbi pointer) from _start
    call kernel_main
    
.hang:
    cli
    hlt
    jmp .hang

; -- 64-bit GDT ------------------------------------------------------------
section .rodata
align 8
gdt64:
    dq 0 ; null
.code: equ $ - gdt64
    dq (1<<43) | (1<<44) | (1<<47) | (1<<53) ; code segment (exec/read, user/sys, present, 64-bit)
.data: equ $ - gdt64
    dq (1<<44) | (1<<47) | (1<<41) ; data segment (user/sys, present, read/write)
.ptr:
    dw $ - gdt64 - 1
    dq gdt64
