; Ring-3 isolation test A: writes a marker value into its own address space,
; then spins forever. Paired with ring3_isolate_b.asm — the kernel launches
; both as separate processes and checks each retains its own value at the
; SAME virtual address, proving per-process address spaces are truly isolated.
; See docs/superpowers/specs/2026-07-20-per-process-address-spaces-design.md
;
; This task deliberately never calls SYS_EXIT. task_exit() frees the task's
; page directory (and every frame under it) as soon as a task's state
; becomes TASK_DEAD, so if this task exited, its marker frame would already
; be gone by the time the kernel inspects it. Spinning forever keeps the
; task's address space alive and inspectable indefinitely instead.
BITS 64
SECTION .text
GLOBAL _start
_start:
    mov rax, 0x8040001000   ; test data address (must match kernel.c's check)
    mov dword [rax], 0xAAAAAAAA
.spin:
    hlt
    jmp .spin
