/* =============================================================================
 * CareOS - drivers/keyboard.c
 * PS/2 keyboard driver — full scancode set 1 translation
 * ============================================================================= */
#include "kernel.h"

/* Circular key buffer */
static char kb_buf[KB_BUF_SIZE];
static u32  kb_head = 0;
static u32  kb_tail = 0;
static bool shift_down   = false;
static bool ctrl_down    = false;
static bool alt_down     = false;
static bool caps_lock    = false;

/* US QWERTY scancode-to-ASCII (unshifted) */
static const char sc_normal[128] = {
    0,  27, '1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,  'a','s','d','f','g','h','j','k','l',';','\'','`',
    0,  '\\','z','x','c','v','b','n','m',',','.','/', 0,
    '*', 0, ' ', 0,
    0,0,0,0,0,0,0,0,0,0,  /* F1-F10 */
    0,0,                   /* NumLock, ScrollLock */
    '7','8','9','-','4','5','6','+','1','2','3','0','.', /* keypad */
    0,0,0,
    0,0                    /* F11,F12 */
};

/* Shifted versions */
static const char sc_shifted[128] = {
    0,  27, '!','@','#','$','%','^','&','*','(',')','_','+','\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0,  'A','S','D','F','G','H','J','K','L',':','"','~',
    0,  '|','Z','X','C','V','B','N','M','<','>','?', 0,
    '*', 0, ' ', 0,
    0,0,0,0,0,0,0,0,0,0,
    0,0,
    '7','8','9','-','4','5','6','+','1','2','3','0','.', 0,0,0,0,0
};

static void kb_push(char c) {
    u32 next = (kb_head + 1) % KB_BUF_SIZE;
    if (next != kb_tail) {
        kb_buf[kb_head] = c;
        kb_head = next;
    }
}

static void keyboard_irq(registers_t *r) {
    (void)r;
    u8 sc = inb(KB_DATA_PORT);
    bool released = (sc & 0x80) != 0;
    u8   key      = sc & 0x7F;

    /* Track modifiers */
    if (key == 0x2A || key == 0x36) { shift_down = !released; return; }
    if (key == 0x1D) { ctrl_down  = !released; return; }
    if (key == 0x38) { alt_down   = !released; return; }
    if (key == 0x3A && !released) { caps_lock = !caps_lock; return; }

    if (released) return;

    if (key < 128) {
        bool use_upper = shift_down ^ caps_lock;
        char c = use_upper ? sc_shifted[key] : sc_normal[key];
        if (c) kb_push(c);
    }
}

void keyboard_init(void) {
    /* Flush any pending data */
    while (inb(KB_STATUS_PORT) & 1) inb(KB_DATA_PORT);
    register_interrupt_handler(IRQ0 + 1, keyboard_irq);
}

bool keyboard_haschar(void) { return kb_head != kb_tail; }

char keyboard_getchar(void) {
    while (!keyboard_haschar()) __asm__ volatile ("hlt");
    char c = kb_buf[kb_tail];
    kb_tail = (kb_tail + 1) % KB_BUF_SIZE;
    return c;
}

void keyboard_flush(void) { kb_head = kb_tail = 0; }


bool keyboard_ctrl_held(void) { return ctrl_down; }
bool keyboard_alt_held(void) { return alt_down; }
bool keyboard_shift_held(void) { return shift_down; }

