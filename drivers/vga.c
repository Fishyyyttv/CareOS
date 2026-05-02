/* =============================================================================
 * CareOS - drivers/vga.c
 * VGA text-mode terminal (80x25, color)
 * ============================================================================= */
#include "kernel.h"

static u32  term_row    = 0;
static u32  term_col    = 0;
static u8   term_color  = 0;
static volatile u16 *vga_buf = VGA_MEMORY;

/* Hardware cursor via CRTC registers */
static void update_hw_cursor(void) {
    u16 pos = (u16)(term_row * VGA_WIDTH + term_col);
    outb(0x3D4, 14);
    outb(0x3D5, (u8)(pos >> 8));
    outb(0x3D4, 15);
    outb(0x3D5, (u8)(pos & 0xFF));
}

void terminal_init(void) {
    term_color = VGA_ENTRY_COLOR(VGA_LIGHT_GREY, VGA_BLACK);
    terminal_clear();
}

void terminal_clear(void) {
    u16 blank = VGA_ENTRY(' ', term_color);
    for (u32 i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
        vga_buf[i] = blank;
    term_row = 0;
    term_col = 0;
    update_hw_cursor();
}

void terminal_setcolor(u8 color) {
    term_color = color;
}

void terminal_scroll(void) {
    /* Move all rows up by one */
    for (u32 r = 1; r < VGA_HEIGHT; r++)
        for (u32 c = 0; c < VGA_WIDTH; c++)
            vga_buf[(r-1)*VGA_WIDTH+c] = vga_buf[r*VGA_WIDTH+c];
    /* Blank bottom row */
    u16 blank = VGA_ENTRY(' ', term_color);
    for (u32 c = 0; c < VGA_WIDTH; c++)
        vga_buf[(VGA_HEIGHT-1)*VGA_WIDTH+c] = blank;
    if (term_row > 0) term_row--;
}

void terminal_putchar(char c) {
    if (c == '\n') {
        term_col = 0;
        term_row++;
    } else if (c == '\r') {
        term_col = 0;
    } else if (c == '\t') {
        term_col = (term_col + 8) & ~7u;
        if (term_col >= VGA_WIDTH) { term_col = 0; term_row++; }
    } else if (c == '\b') {
        if (term_col > 0) {
            term_col--;
            vga_buf[term_row * VGA_WIDTH + term_col] = VGA_ENTRY(' ', term_color);
        }
    } else {
        vga_buf[term_row * VGA_WIDTH + term_col] = VGA_ENTRY((u8)c, term_color);
        term_col++;
        if (term_col >= VGA_WIDTH) { term_col = 0; term_row++; }
    }
    while (term_row >= VGA_HEIGHT) terminal_scroll();
    update_hw_cursor();
}

void terminal_write(const char *str) {
    if (!str) return;
    while (*str) terminal_putchar(*str++);
}

void terminal_writeln(const char *str) {
    terminal_write(str);
    terminal_putchar('\n');
}

void terminal_write_colored(const char *str, u8 color) {
    u8 saved = term_color;
    term_color = color;
    terminal_write(str);
    term_color = saved;
}

void terminal_set_cursor(u32 col, u32 row) {
    term_col = col < VGA_WIDTH  ? col : VGA_WIDTH-1;
    term_row = row < VGA_HEIGHT ? row : VGA_HEIGHT-1;
    update_hw_cursor();
}

/* ── kprintf / ksprintf ─────────────────────────────────────────────────── */
int kvsnprintf(char *out, size_t max, const char *fmt, va_list ap) {
    char *p = out;
    char *end = out + max - 1;
    char tmp[32];

    for (const char *f = fmt; *f && (p < end || !out); f++) {
        if (*f != '%') { if(out) *p++ = *f; else terminal_putchar(*f); continue; }
        f++;
        int pad = 0;
        if (*f == '0') { f++; while(*f >= '0' && *f <= '9') { pad = pad * 10 + (*f - '0'); f++; } }
        
        switch (*f) {
        case 'd':
        case 'u': {
            u32 v = va_arg(ap, u32);
            kutoa(v, tmp, 10);
            int len = (int)kstrlen(tmp);
            while(pad > len) { if(out) *p++ = '0'; else terminal_putchar('0'); pad--; }
            for(char *s = tmp; *s; s++) { if(out) *p++ = *s; else terminal_putchar(*s); }
            break;
        }
        case 'x':
        case 'X':
        case 'p': {
            u32 v = va_arg(ap, u32);
            if(*f == 'p') { if(out) { *p++='0';*p++='x'; } else terminal_write("0x"); }
            kutoa(v, tmp, 16);
            if(*f == 'X') for(char *s=tmp;*s;s++) if(*s>='a'&&*s<='f') *s-=32;
            for(char *s = tmp; *s; s++) { if(out) *p++ = *s; else terminal_putchar(*s); }
            break;
        }
        case 's': {
            const char *s = va_arg(ap, const char*);
            if(!s) s = "(null)";
            while(*s) { if(out) *p++ = *s++; else terminal_putchar(*s++); }
            break;
        }
        case 'c': { if(out) *p++ = (char)va_arg(ap, int); else terminal_putchar((char)va_arg(ap, int)); break; }
        case '%': { if(out) *p++ = '%'; else terminal_putchar('%'); break; }
        default:  { if(out) *p++ = *f; else terminal_putchar(*f); break; }
        }
    }
    if(out) *p = '\0';
    return out ? (int)(p - out) : 0;
}

void kprintf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    kvsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
}

int ksprintf(char *buf, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = kvsnprintf(buf, 1024, fmt, ap);
    va_end(ap);
    return n;
}
