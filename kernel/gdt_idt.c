/* =============================================================================
 * CareOS - kernel/gdt_idt.c
 * Global Descriptor Table and Interrupt Descriptor Table setup for x86_64
 * ============================================================================= */
#include "kernel.h"

/* ── GDT ──────────────────────────────────────────────────────────────────── */
/* In x86_64, code/data segments are mostly 8 bytes, but TSS is 16 bytes.
   We'll use a simple layout: 0=null, 1=kcode, 2=kdata, 3=ucode, 4=udata, 5=TSS(16-byte) */
u64 gdt[8]; 
gdt_ptr_t gdt_ptr;

static void gdt_set_entry(int i, u64 base, u32 limit, u8 access, u8 gran) {
    gdt[i] = (limit & 0xFFFF);
    gdt[i] |= ((base & 0xFFFF) << 16);
    gdt[i] |= ((base & 0xFF0000) << 16);
    gdt[i] |= ((u64)access << 40);
    gdt[i] |= (((u64)(limit >> 16) & 0x0F) << 48);
    gdt[i] |= ((u64)(gran & 0xF0) << 48);
    gdt[i] |= ((base >> 24) << 56);
}

static void gdt_set_tss(int i, u64 base, u32 limit) {
    gdt_set_entry(i, base, limit, 0x89, 0x40);
    gdt[i+1] = (base >> 32);
}

void gdt_init(void) {
    gdt_ptr.limit = (u16)(sizeof(gdt) - 1);
    gdt_ptr.base  = (u64)&gdt;

    gdt[0] = 0; // Null
    gdt_set_entry(1, 0, 0xFFFFFFFF, 0x9A, 0x20); // Kernel Code (L=1)
    gdt_set_entry(2, 0, 0xFFFFFFFF, 0x92, 0x00); // Kernel Data
    gdt_set_entry(3, 0, 0xFFFFFFFF, 0xFA, 0x20); // User Code (L=1)
    gdt_set_entry(4, 0, 0xFFFFFFFF, 0xF2, 0x00); // User Data

    __asm__ volatile (
        "lgdt %0\n"
        "pushq $0x08\n"
        "pushq $1f\n"
        "lretq\n"
        "1:\n"
        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "mov %%ax, %%ss\n"
        : : "m"(gdt_ptr) : "rax", "memory"
    );
}

/* ── IDT ──────────────────────────────────────────────────────────────────── */
static idt_entry_t idt[IDT_ENTRIES];
static idt_ptr_t   idt_ptr;
static isr_handler_t isr_handlers[IDT_ENTRIES];

void idt_set_gate64(u8 num, u64 base, u16 sel, u8 flags) {
    idt[num].base_low  = (u16)(base & 0xFFFF);
    idt[num].selector  = sel;
    idt[num].ist       = 0;
    idt[num].flags     = flags | 0x60; // DPL=3 so userspace can trigger if needed (though usually 0x8E)
    idt[num].base_mid  = (u16)((base >> 16) & 0xFFFF);
    idt[num].base_high = (u32)((base >> 32) & 0xFFFFFFFF);
    idt[num].reserved  = 0;
}

void register_interrupt_handler(u8 n, isr_handler_t h) {
    isr_handlers[n] = h;
}

/* ── ISR / IRQ stubs (64-bit ASM) ─────────────────────────────────────────── */

__asm__ (
    ".macro PUSH_ALL\n"
    "  push %rax; push %rbx; push %rcx; push %rdx\n"
    "  push %rsi; push %rdi; push %rbp; push %r8\n"
    "  push %r9;  push %r10; push %r11; push %r12\n"
    "  push %r13; push %r14; push %r15\n"
    ".endm\n"
    ".macro POP_ALL\n"
    "  pop %r15; pop %r14; pop %r13; pop %r12\n"
    "  pop %r11; pop %r10; pop %r9;  pop %r8\n"
    "  pop %rbp; pop %rdi; pop %rsi; pop %rdx\n"
    "  pop %rcx; pop %rbx; pop %rax\n"
    ".endm\n"
);

#define ISR_NOERR(num) \
    void isr##num##_stub(void); \
    __asm__( \
        ".global isr"#num"_stub\n" \
        "isr"#num"_stub:\n" \
        "  pushq $0\n" \
        "  pushq $"#num"\n" \
        "  jmp isr_common_stub\n" \
    );

#define ISR_ERR(num) \
    void isr##num##_stub(void); \
    __asm__( \
        ".global isr"#num"_stub\n" \
        "isr"#num"_stub:\n" \
        "  pushq $"#num"\n" \
        "  jmp isr_common_stub\n" \
    );

#define IRQ(num, mapped) \
    void irq##num##_stub(void); \
    __asm__( \
        ".global irq"#num"_stub\n" \
        "irq"#num"_stub:\n" \
        "  pushq $0\n" \
        "  pushq $"#mapped"\n" \
        "  jmp irq_common_stub\n" \
    );

__asm__ (
    ".global isr_common_stub\n"
    "isr_common_stub:\n"
    "  PUSH_ALL\n"
    "  mov %rsp, %rdi\n"
    "  call isr_handler\n"
    "  POP_ALL\n"
    "  add $16, %rsp\n"
    "  iretq\n"
);

__asm__ (
    "irq_common_stub:\n"
    "  PUSH_ALL\n"
    "  mov %rsp, %rdi\n"
    "  call irq_handler\n"
    "  POP_ALL\n"
    "  add $16, %rsp\n"
    "  iretq\n"
);

/* Exception stubs 0-19 */
ISR_NOERR(0)  ISR_NOERR(1)  ISR_NOERR(2)  ISR_NOERR(3)
ISR_NOERR(4)  ISR_NOERR(5)  ISR_NOERR(6)  ISR_NOERR(7)
ISR_ERR(8)    ISR_NOERR(9)  ISR_ERR(10)   ISR_ERR(11)
ISR_ERR(12)   ISR_ERR(13)   ISR_ERR(14)   ISR_NOERR(15)
ISR_NOERR(16) ISR_ERR(17)   ISR_NOERR(18) ISR_NOERR(19)

/* IRQ stubs 0-15 */
IRQ(0,32)  IRQ(1,33)  IRQ(2,34)  IRQ(3,35)
IRQ(4,36)  IRQ(5,37)  IRQ(6,38)  IRQ(7,39)
IRQ(8,40)  IRQ(9,41)  IRQ(10,42) IRQ(11,43)
IRQ(12,44) IRQ(13,45) IRQ(14,46) IRQ(15,47)

static const char *exception_msgs[] = {
    "Division By Zero", "Debug", "Non Maskable Interrupt", "Breakpoint",
    "Into Detected Overflow", "Out of Bounds", "Invalid Opcode", "No Coprocessor",
    "Double Fault", "Coprocessor Segment Overrun", "Bad TSS", "Segment Not Present",
    "Stack Fault", "General Protection Fault", "Page Fault", "Unknown Interrupt",
    "Coprocessor Fault", "Alignment Check", "Machine Check", "Reserved"
};

void isr_handler(registers_t *r) {
    if (isr_handlers[r->int_no]) {
        isr_handlers[r->int_no](r);
    } else if (r->int_no < 20) {
        serial_write("[EXCEPTION] ");
        serial_write(exception_msgs[r->int_no]);
        serial_write("\n");
        kernel_panic(0x100u + r->int_no, exception_msgs[r->int_no]);
    }
}

/* ── PIC ─────────────────────────────────────────────────────────────────── */
void pic_init(void) {
    outb(PIC1_CMD,  0x11); io_wait();
    outb(PIC2_CMD,  0x11); io_wait();
    outb(PIC1_DATA, 0x20); io_wait();
    outb(PIC2_DATA, 0x28); io_wait();
    outb(PIC1_DATA, 0x04); io_wait();
    outb(PIC2_DATA, 0x02); io_wait();
    outb(PIC1_DATA, 0x01); io_wait();
    outb(PIC2_DATA, 0x01); io_wait();
    outb(PIC1_DATA, 0xFC);
    outb(PIC2_DATA, 0xFF);
}

void pic_send_eoi(u8 irq) {
    if (irq >= 8) outb(PIC2_CMD, PIC_EOI);
    outb(PIC1_CMD, PIC_EOI);
}

void irq_handler(registers_t *r) {
    u8 irq = (u8)(r->int_no - IRQ0);
    if (isr_handlers[r->int_no])
        isr_handlers[r->int_no](r);
    pic_send_eoi(irq);
}

void idt_init(void) {
    idt_ptr.limit = (u16)(sizeof(idt) - 1);
    idt_ptr.base  = (u64)idt;

    kmemset(idt, 0, sizeof(idt));
    kmemset(isr_handlers, 0, sizeof(isr_handlers));

    #define SET_ISR(n) idt_set_gate64(n, (u64)isr##n##_stub, GDT_CODE_SEG, 0x8E)
    #define SET_IRQ(n, m) idt_set_gate64(m, (u64)irq##n##_stub, GDT_CODE_SEG, 0x8E)

    SET_ISR(0);  SET_ISR(1);  SET_ISR(2);  SET_ISR(3);
    SET_ISR(4);  SET_ISR(5);  SET_ISR(6);  SET_ISR(7);
    SET_ISR(8);  SET_ISR(9);  SET_ISR(10); SET_ISR(11);
    SET_ISR(12); SET_ISR(13); SET_ISR(14); SET_ISR(15);
    SET_ISR(16); SET_ISR(17); SET_ISR(18); SET_ISR(19);

    SET_IRQ(0,32);  SET_IRQ(1,33);  SET_IRQ(2,34);  SET_IRQ(3,35);
    SET_IRQ(4,36);  SET_IRQ(5,37);  SET_IRQ(6,38);  SET_IRQ(7,39);
    SET_IRQ(8,40);  SET_IRQ(9,41);  SET_IRQ(10,42); SET_IRQ(11,43);
    SET_IRQ(12,44); SET_IRQ(13,45); SET_IRQ(14,46); SET_IRQ(15,47);

    pic_init();

    __asm__ volatile ("lidt %0" : : "m"(idt_ptr));
    __asm__ volatile ("sti");
}

/* Backward compatibility for idt_set_gate if used elsewhere with u32 */
void idt_set_gate(u8 num, u32 base, u16 sel, u8 flags) {
    idt_set_gate64(num, (u64)base, sel, flags);
}
