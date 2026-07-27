/* Multiboot2 kernel linker script */
ENTRY(_start)

SECTIONS {
    . = 1M;

    .multiboot2 : {
        KEEP(*(.multiboot2))
    }

    .text ALIGN(4K) : {
        *(.text*)
    }

    .rodata ALIGN(4K) : {
        *(.rodata*)
    }

    .data ALIGN(4K) : {
        *(.data*)
    }

    .bss ALIGN(4K) : {
        *(COMMON)
        *(.bss*)
    }

    /* First byte of physical memory the kernel image does NOT occupy.
     *
     * paging_init() reserves everything below this from the physical frame
     * allocator. It used to reserve a hardcoded 256 MB, which was a silent
     * trap: .bss is dominated by heap_mem[KERNEL_HEAP_SIZE], and .rodata now
     * carries the baked asset archive, so the image end MOVES whenever either
     * grows. Crossing the constant would have handed heap_mem's own pages out
     * as free frames. Deriving it from the link makes that unrepresentable. */
    . = ALIGN(4K);
    _kernel_end = .;

    /DISCARD/ : {
        *(.eh_frame)
        *(.comment)
    }
}
