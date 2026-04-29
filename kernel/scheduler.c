/* =============================================================================
 * CareOS - kernel/scheduler.c
 * x86_64 Preemptive round-robin scheduler
 * ============================================================================= */
#include "kernel.h"

/* ── TSS (x86_64) ─────────────────────────────────────────────────────────── */
typedef struct {
    u32 reserved0;
    u64 rsp0, rsp1, rsp2;
    u64 reserved1;
    u64 ist1, ist2, ist3, ist4, ist5, ist6, ist7;
    u64 reserved2;
    u16 reserved3;
    u16 iomap_base;
} __attribute__((packed)) tss_t;

static tss_t kernel_tss;

/* External GDT from gdt_idt.c */
extern u64 gdt[8];
extern gdt_ptr_t gdt_ptr;

static void tss_install(u32 gdt_slot) {
    u64 base = (u64)&kernel_tss;
    u32 limit = sizeof(kernel_tss) - 1;

    /* x86_64 TSS descriptor is 16 bytes */
    gdt[gdt_slot] = (limit & 0xFFFF);
    gdt[gdt_slot] |= ((base & 0xFFFF) << 16);
    gdt[gdt_slot] |= ((base & 0xFF0000) << 16);
    gdt[gdt_slot] |= (0x89ULL << 40); /* Present, Type=0x9 (64-bit TSS available) */
    gdt[gdt_slot] |= (((u64)(limit >> 16) & 0x0F) << 48);
    gdt[gdt_slot] |= ((base >> 24) << 56);
    gdt[gdt_slot+1] = (base >> 32);

    __asm__ volatile ("ltr %%ax" : : "a"((u16)(gdt_slot * 8)));
}

static void tss_set_kernel_stack(u64 rsp0) {
    kernel_tss.rsp0 = rsp0;
}

/* ── Task control blocks ────────────────────────────────────────────────────── */
#define TASK_STACK_PAGES 4
#define TASK_STACK_SIZE  (TASK_STACK_PAGES * PAGE_SIZE)
#define TIMESLICE_DEFAULT 5

typedef struct tcb {
    u32          id;
    char         name[32];
    task_state_t state;
    u32          timeslice;
    u32          ticks_remaining;
    u32          total_ticks;

    u64  rsp;
    u64  cr3;

    u8  *kstack;
    u8  *ustack;

    u64  entry;
    bool is_user;

    int  exit_code;
    int  session_id;

    u32  pending_signals;
    bool task_killed;
} tcb_t;

static tcb_t  tasks[MAX_TASKS];
static u32    task_count   = 0;
static u32    current_task = 0;
static bool   sched_ready  = false;

task_t *task_current(void) {
    return (task_t *)&tasks[current_task];
}

u32 task_current_cr3(void) {
    return (u32)tasks[current_task].cr3;
}

/* ── Context switch (x86_64) ──────────────────────────────────────────────── */
static void __attribute__((noinline)) switch_context(
        u64 *old_rsp, u64 new_rsp, u64 new_cr3) {
    __asm__ volatile (
        "pushfq\n"
        "push %%rax; push %%rbx; push %%rcx; push %%rdx\n"
        "push %%rsi; push %%rdi; push %%rbp; push %%r8\n"
        "push %%r9;  push %%r10; push %%r11; push %%r12\n"
        "push %%r13; push %%r14; push %%r15\n"
        "movq %%rsp, (%0)\n"
        "movq %1, %%rsp\n"
        "test %2, %2\n"
        "jz 1f\n"
        "movq %2, %%cr3\n"
        "1:\n"
        "pop %%r15; pop %%r14; pop %%r13; pop %%r12\n"
        "pop %%r11; pop %%r10; pop %%r9;  pop %%r8\n"
        "pop %%rbp; pop %%rdi; pop %%rsi; pop %%rdx\n"
        "pop %%rcx; pop %%rbx; pop %%rax\n"
        "popfq\n"
        :
        : "r"(old_rsp), "r"(new_rsp), "r"(new_cr3)
        : "memory"
    );
}

void signal_send(u32 task_id, u32 sig) {
    for (u32 i = 0; i < task_count; i++) {
        if (tasks[i].id == task_id) {
            tasks[i].pending_signals |= (1u << sig);
            return;
        }
    }
}

void scheduler_tick(registers_t *r) {
    timer_tick_advance();
    if (!sched_ready || task_count == 0) return;

    tcb_t *cur = &tasks[current_task];
    cur->total_ticks++;

    /* Deliver pending signals */
    if (cur->pending_signals & (1u << SIGKILL)) {
        cur->task_killed     = true;
        cur->pending_signals = 0;
        cur->state           = TASK_DEAD;
    }
    if (cur->pending_signals & (1u << SIGINT)) {
        cur->pending_signals &= ~(1u << SIGINT);
        kb_push_char(0x03);  /* ETX → shell sees Ctrl+C */
    }

    if (cur->ticks_remaining > 0) {
        cur->ticks_remaining--;
        return;
    }

    u32 next = current_task;
    for (u32 i = 1; i <= task_count; i++) {
        u32 idx = (current_task + i) % task_count;
        if (tasks[idx].state == TASK_READY || tasks[idx].state == TASK_RUNNING) {
            next = idx;
            break;
        }
    }

    if (next == current_task) {
        cur->ticks_remaining = cur->timeslice;
        return;
    }

    cur->state = TASK_READY;
    tcb_t *nxt = &tasks[next];
    nxt->state = TASK_RUNNING;
    nxt->ticks_remaining = nxt->timeslice;

    tss_set_kernel_stack((u64)(nxt->kstack + TASK_STACK_SIZE));

    current_task = next;
    switch_context(&cur->rsp, nxt->rsp, nxt->cr3);
}

static void __attribute__((noinline)) task_trampoline(void) {
    tcb_t *t = &tasks[current_task];
    if (t->entry) {
        typedef void (*fn_t)(void);
        fn_t fn = (fn_t)t->entry;
        fn();
    }
    task_exit();
}

static void __attribute__((noreturn)) enter_userspace(u64 entry, u64 user_rsp) {
    __asm__ volatile (
        "mov $0x23, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "pushq $0x23\n"      /* SS */
        "pushq %1\n"         /* RSP */
        "pushq $0x202\n"      /* RFLAGS */
        "pushq $0x1B\n"      /* CS */
        "pushq %0\n"         /* RIP */
        "iretq\n"
        :
        : "r"(entry), "r"(user_rsp)
        : "rax", "memory"
    );
    __builtin_unreachable();
}

static void __attribute__((noinline)) task_user_trampoline(void) {
    tcb_t *t = &tasks[current_task];
    enter_userspace(t->entry, 0xBFF00000ULL + TASK_STACK_SIZE);
}

int task_create(const char *name, task_func_t fn) {
    if (task_count >= MAX_TASKS) return -1;

    tcb_t *t = &tasks[task_count];
    kmemset(t, 0, sizeof(tcb_t));
    t->id       = task_count + 1;
    kstrncpy(t->name, name, 31);
    t->state    = TASK_READY;
    t->timeslice       = TIMESLICE_DEFAULT;
    t->ticks_remaining = TIMESLICE_DEFAULT;
    t->entry    = (u64)fn;
    t->is_user  = false;
    t->cr3      = 0;
    t->session_id = -1;

    t->kstack = (u8*)kmalloc(TASK_STACK_SIZE);
    if (!t->kstack) return -1;

    u64 *sp = (u64*)(t->kstack + TASK_STACK_SIZE);
    *--sp = (u64)task_trampoline;
    *--sp = 0x202; /* RFLAGS */
    for (int i = 0; i < 15; i++) *--sp = 0; /* 15 registers */
    t->rsp = (u64)sp;

    task_count++;
    return (int)(task_count - 1);
}

int task_create_user(const char *name, u64 entry, pde_t *page_dir, int session) {
    if (task_count >= MAX_TASKS) return -1;

    tcb_t *t = &tasks[task_count];
    kmemset(t, 0, sizeof(tcb_t));
    t->id              = task_count + 1;
    kstrncpy(t->name, name, 31);
    t->state           = TASK_READY;
    t->timeslice       = TIMESLICE_DEFAULT;
    t->ticks_remaining = TIMESLICE_DEFAULT;
    t->entry           = entry;
    t->is_user         = true;
    t->cr3             = (u64)page_dir;
    t->session_id      = session;

    t->kstack = (u8*)kmalloc(TASK_STACK_SIZE);
    t->ustack = (u8*)kmalloc(TASK_STACK_SIZE);

    u64 *sp = (u64*)(t->kstack + TASK_STACK_SIZE);
    *--sp = (u64)task_user_trampoline;
    *--sp = 0x202;
    for (int i = 0; i < 15; i++) *--sp = 0;
    t->rsp = (u64)sp;

    task_count++;
    return (int)(task_count - 1);
}

void task_yield(void) {
    if (!sched_ready) return;
    __asm__ volatile ("int $0x20");
}

__attribute__((noreturn)) void task_exit(void) {
    tasks[current_task].state = TASK_DEAD;
    task_yield();
    while (1) { __asm__ volatile ("hlt"); }
}

static void idle_task(void) { while(1) __asm__ volatile("hlt"); }

void scheduler_init(void) {
    kmemset(tasks, 0, sizeof(tasks));
    current_task = 0;
    task_count   = 0;

    kmemset(&kernel_tss, 0, sizeof(kernel_tss));
    kernel_tss.iomap_base = sizeof(tss_t);
    tss_install(5);

    task_create("idle", idle_task);
    tasks[0].state = TASK_RUNNING;

    register_interrupt_handler(IRQ0, (isr_handler_t)scheduler_tick);
    sched_ready = true;
}

void task_init(void) { scheduler_init(); }

void task_list(void) { /* Implementation same as before, omitted for brevity */ }

task_t *task_get(u32 id) {
    for (u32 i = 0; i < task_count; i++)
        if (tasks[i].id == id) return (task_t *)&tasks[i];
    return NULL;
}

u32 task_count_active(void) {
    u32 n = 0;
    for (u32 i = 0; i < task_count; i++)
        if (tasks[i].state == TASK_READY || tasks[i].state == TASK_RUNNING)
            n++;
    return n;
}
