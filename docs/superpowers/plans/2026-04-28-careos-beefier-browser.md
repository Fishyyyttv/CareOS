# CareOS v9 — Beefier OS + Browser Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add pipes, signals, BGA graphics mode switching, file manager improvements, editor search/replace/dialog, real package install/remove, and browser tabs/bookmarks/find/chunked-HTTP/HTML.

**Architecture:** Foundation-first — kernel primitives (pipes, signals) first, then BGA graphics (no real-mode trampoline needed; QEMU's `-vga std` exposes Bochs Graphics Adapter I/O ports usable from 64-bit long mode), then app-layer improvements on top. All changes are additive; existing behaviour is preserved when features aren't activated.

**Tech Stack:** C (freestanding), NASM (elf64), QEMU x86_64, GCC cross-compile. Build with `make`, test with `make run`.

---

## File Map

| File | Status | Role |
|------|--------|------|
| `kernel/pipe.c` | CREATE | Pipe buffer implementation |
| `kernel/scheduler.c` | MODIFY | Add signal delivery per tick |
| `drivers/keyboard.c` | MODIFY | Route Ctrl+C → SIGINT |
| `drivers/vesa.c` | CREATE | BGA mode switch (port 0x1CE/0x1CF) |
| `shell/shell.c` | MODIFY | Pipe parser, kill built-in, SIGINT handler |
| `include/kernel.h` | MODIFY | pipe_t, signal fields in task_t, vesa API, carepkg, clip_mode |
| `gui/gui.h` | MODIFY | window_t new fields (browser tabs, editor find/dialog, fm rename) |
| `gui/wm.c` | MODIFY | drag_file_t global, app_browser_mouse call site |
| `apps/app_files.c` | MODIFY | Copy/paste, inline rename, drag-and-drop, preview pane |
| `apps/app_editor.c` | MODIFY | Syntax highlight, search/replace, open/save dialog |
| `apps/app_settings.c` | MODIFY | Display tab with BGA mode list |
| `apps/app_browser.c` | MODIFY | Tabs, bookmarks, find, mouse wheel, chunked, HTML |
| `apps/app_pkgmgr.c` | MODIFY | Tabs, detail pane, progress |
| `kernel/carepkg.c` | MODIFY | Real install/remove, deps, /var/pkg/installed.db |
| `kernel/settings.c` | MODIFY | Add vesa_mode field |
| `kernel/kernel.c` | MODIFY | Call vesa_init(), create /usr/bin /var/pkg |
| `net/net.c` | MODIFY | http_decode_chunked() |
| `Makefile` | MODIFY | Add pipe.c and vesa.c to C_SRC |

---

## Task 1: Kernel Pipes

**Files:**
- Create: `kernel/pipe.c`
- Modify: `include/kernel.h` (add pipe_t and API)
- Modify: `Makefile` (add kernel/pipe.c to C_SRC)

- [ ] **Step 1: Add pipe_t and API to kernel.h**

Add after the `/* -- Extra kernel helpers */` section in `include/kernel.h`:

```c
/* -- Pipes ---------------------------------------------------------------- */
#define PIPE_BUF_SIZE 4096

typedef struct {
    char buf[PIPE_BUF_SIZE];
    u32  len;
    bool closed;
} pipe_t;

pipe_t *pipe_create(void);
int     pipe_write(pipe_t *p, const char *data, u32 len);
int     pipe_read(pipe_t *p, char *out, u32 maxlen);
void    pipe_close(pipe_t *p);
void    pipe_reset(pipe_t *p);
```

- [ ] **Step 2: Write kernel/pipe.c**

```c
/* CareOS v9 -- kernel/pipe.c -- Buffered sequential pipe */
#include "kernel.h"

static pipe_t pipe_pool[8];
static bool   pipe_used[8];

pipe_t *pipe_create(void) {
    for (int i = 0; i < 8; i++) {
        if (!pipe_used[i]) {
            pipe_used[i] = true;
            kmemset(&pipe_pool[i], 0, sizeof(pipe_t));
            return &pipe_pool[i];
        }
    }
    return NULL;
}

int pipe_write(pipe_t *p, const char *data, u32 len) {
    if (!p || p->closed) return -1;
    u32 space = PIPE_BUF_SIZE - p->len;
    if (len > space) len = space;
    kmemcpy(p->buf + p->len, data, len);
    p->len += len;
    return (int)len;
}

int pipe_read(pipe_t *p, char *out, u32 maxlen) {
    if (!p) return -1;
    u32 n = p->len < maxlen ? p->len : maxlen;
    kmemcpy(out, p->buf, n);
    out[n] = '\0';
    return (int)n;
}

void pipe_close(pipe_t *p) {
    if (!p) return;
    for (int i = 0; i < 8; i++) {
        if (&pipe_pool[i] == p) { pipe_used[i] = false; break; }
    }
    p->closed = true;
}

void pipe_reset(pipe_t *p) {
    if (!p) return;
    p->len = 0;
    p->closed = false;
}
```

- [ ] **Step 3: Add kernel/pipe.c to Makefile**

In `Makefile`, add `kernel/pipe.c \` after `kernel/ext2.c \` in the C_SRC list.

- [ ] **Step 4: Verify it compiles**

```bash
make clean && make 2>&1 | tail -20
```
Expected: no errors, `careos.iso` created.

- [ ] **Step 5: Commit**

```bash
git add kernel/pipe.c include/kernel.h Makefile
git commit -m "feat: add kernel pipe buffers"
```

---

## Task 2: Signals (SIGINT + SIGKILL)

**Files:**
- Modify: `include/kernel.h` (signal fields in task_t public view, signal_send prototype)
- Modify: `kernel/scheduler.c` (add pending_signals + task_killed to tcb_t, deliver per tick)
- Modify: `drivers/keyboard.c` (Ctrl+C pushes special char 0x03)
- Modify: `shell/shell.c` (detect 0x03 to abort command, `kill` built-in)

- [ ] **Step 1: Add signal_send to kernel.h**

Add to `include/kernel.h` after the pipe section:

```c
/* -- Signals -------------------------------------------------------------- */
#define SIGINT  2
#define SIGKILL 9

void signal_send(u32 task_id, u32 sig);
```

- [ ] **Step 2: Extend tcb_t in scheduler.c and add signal delivery**

In `kernel/scheduler.c`, add two fields to `tcb_t` struct (after `session_id`):

```c
    u32  pending_signals;
    bool task_killed;
```

Add `signal_send` implementation just before `scheduler_tick`:

```c
void signal_send(u32 task_id, u32 sig) {
    for (u32 i = 0; i < task_count; i++) {
        if (tasks[i].id == task_id) {
            tasks[i].pending_signals |= (1u << sig);
            return;
        }
    }
}
```

In `scheduler_tick`, add signal check after `cur->total_ticks++`:

```c
    /* Deliver pending signals */
    if (cur->pending_signals & (1u << SIGKILL)) {
        cur->task_killed = true;
        cur->pending_signals = 0;
        cur->state = TASK_DEAD;
    }
    if (cur->pending_signals & (1u << SIGINT)) {
        cur->pending_signals &= ~(1u << SIGINT);
        /* Push ETX (Ctrl+C) into keyboard buffer so shell loop sees it */
        extern void kb_push_char(char c);
        kb_push_char(0x03);
    }
```

- [ ] **Step 3: Expose kb_push_char from keyboard.c**

In `drivers/keyboard.c`, change `static void kb_push` to `void kb_push_char` and add a forward declaration. Replace:

```c
static void kb_push(char c) {
```
with:
```c
void kb_push_char(char c) {
```

Update all internal callers: change `kb_push(c)` to `kb_push_char(c)`.

Add `void kb_push_char(char c);` to `include/kernel.h` under the keyboard section.

- [ ] **Step 4: Handle Ctrl+C (0x03) in shell.c**

In `shell/shell.c`, in the main character-reading loop where characters are processed, add at the top of the input-handling block:

```c
        if (c == 0x03) {  /* Ctrl+C / SIGINT */
            terminal_write("^C\n");
            line_buf[0] = '\0';
            line_len = 0;
            print_prompt();
            continue;
        }
```

Add `kill` built-in command. In the command dispatcher section (where `if (kstrcmp(args[0],"ls")==0)` etc. live), add:

```c
        else if (kstrcmp(args[0], "kill") == 0) {
            if (argc < 2) { terminal_writeln("kill: usage: kill <pid>"); }
            else {
                u32 pid = (u32)katoi(args[1]);
                signal_send(pid, SIGKILL);
                char msg[48];
                ksprintf(msg, "kill: sent SIGKILL to %u\n", pid);
                terminal_write(msg);
            }
        }
```

- [ ] **Step 5: Build and verify**

```bash
make clean && make 2>&1 | tail -20
```
Expected: clean build.

- [ ] **Step 6: Commit**

```bash
git add include/kernel.h kernel/scheduler.c drivers/keyboard.c shell/shell.c
git commit -m "feat: add SIGINT/SIGKILL signals and Ctrl+C handling"
```

---

## Task 3: Pipe Support in Shell

**Files:**
- Modify: `shell/shell.c` (detect `|`, run left into pipe_buf, feed pipe_buf to right)

- [ ] **Step 1: Add pipe execution to shell.c**

In `shell/shell.c`, add a pipe buffer and helper at the top of the file (after the static declarations):

```c
static char   pipe_out_buf[PIPE_BUF_SIZE];
static u32    pipe_out_len = 0;
static bool   pipe_stdout_active = false;

/* Called by built-in commands to write output; redirects to pipe if active */
static void shell_out(const char *s) {
    if (pipe_stdout_active) {
        u32 l = (u32)kstrlen(s);
        if (pipe_out_len + l < PIPE_BUF_SIZE - 1) {
            kmemcpy(pipe_out_buf + pipe_out_len, s, l);
            pipe_out_len += l;
            pipe_out_buf[pipe_out_len] = '\0';
        }
    } else {
        terminal_write(s);
    }
}
```

Replace all `terminal_write` / `terminal_writeln` calls inside built-in command handlers with `shell_out(...)` (except for the prompt and error messages that should always go to terminal).

Add pipe dispatch at the start of command execution (before argument parsing):

```c
        /* Pipe detection: split on first | */
        char *pipe_pos = NULL;
        for (u32 pi = 0; pi < line_len; pi++) {
            if (line_buf[pi] == '|') { pipe_pos = &line_buf[pi]; break; }
        }
        if (pipe_pos) {
            *pipe_pos = '\0';
            char left_cmd[MAX_LINE], right_cmd[MAX_LINE];
            kstrncpy(left_cmd, line_buf, MAX_LINE - 1);
            kstrncpy(right_cmd, pipe_pos + 1, MAX_LINE - 1);
            /* Trim leading space from right */
            char *rp = right_cmd;
            while (*rp == ' ') rp++;

            /* Run left command, capture output */
            pipe_out_len = 0; pipe_out_buf[0] = '\0';
            pipe_stdout_active = true;
            /* Re-invoke execute with left_cmd */
            kstrncpy(line_buf, left_cmd, MAX_LINE - 1);
            line_len = (u32)kstrlen(line_buf);
            /* (fall through to normal execution, output goes to pipe_out_buf) */
            pipe_stdout_active = false;

            /* Feed pipe output as stdin-like prefix for right command */
            /* For grep-like commands: store in a global the right reads */
            /* Simple: pass pipe_out_buf via a static for built-in 'grep' */
            extern char g_pipe_stdin[PIPE_BUF_SIZE];
            extern bool g_pipe_stdin_active;
            kmemcpy(g_pipe_stdin, pipe_out_buf, pipe_out_len + 1);
            g_pipe_stdin_active = true;

            kstrncpy(line_buf, rp, MAX_LINE - 1);
            line_len = (u32)kstrlen(line_buf);
            /* (fall through runs right command reading from g_pipe_stdin) */
            g_pipe_stdin_active = false;
            hist_push(); line_buf[0]='\0'; line_len=0;
            print_prompt();
            continue;
        }
```

Add `grep` built-in that uses pipe stdin:

```c
static char g_pipe_stdin[PIPE_BUF_SIZE];
static bool g_pipe_stdin_active = false;
```

```c
        else if (kstrcmp(args[0], "grep") == 0) {
            if (argc < 2) { terminal_writeln("grep: usage: grep <pattern>"); }
            else {
                const char *src = g_pipe_stdin_active ? g_pipe_stdin : "";
                const char *pattern = args[1];
                const char *p = src;
                while (*p) {
                    const char *line_start = p;
                    while (*p && *p != '\n') p++;
                    u32 llen = (u32)(p - line_start);
                    char tmp[256]; if (llen > 255) llen = 255;
                    kmemcpy(tmp, line_start, llen); tmp[llen] = '\0';
                    /* Simple substring search */
                    bool found = false;
                    for (u32 ci = 0; ci + kstrlen(pattern) <= llen; ci++) {
                        if (kstrncmp(tmp + ci, pattern, kstrlen(pattern)) == 0) { found = true; break; }
                    }
                    if (found) { terminal_write(tmp); terminal_putchar('\n'); }
                    if (*p == '\n') p++;
                }
            }
        }
```

- [ ] **Step 2: Build and verify**

```bash
make clean && make 2>&1 | tail -20
```

- [ ] **Step 3: Commit**

```bash
git add shell/shell.c
git commit -m "feat: add pipe operator and grep built-in to shell"
```

---

## Task 4: BGA Graphics Mode Switching

**Files:**
- Create: `drivers/vesa.c`
- Modify: `include/kernel.h` (vesa_mode_t, API)
- Modify: `kernel/settings.c` (add vesa_mode field)
- Modify: `kernel/kernel.c` (call vesa_init)
- Modify: `Makefile` (add drivers/vesa.c)

The Bochs Graphics Adapter (BGA) is implemented in QEMU's `-vga std`. It uses two I/O ports:
- `0x01CE` — index port
- `0x01CF` — data port (16-bit)

Index registers:
- `0` = ID (read 0xB0C0–0xB0C5)
- `1` = XRES
- `2` = YRES
- `3` = BPP
- `4` = ENABLE (0=disable, 1=enable+clear, 3=enable+noclear+lfb)
- `6` = VIRT_WIDTH
- `7` = VIRT_HEIGHT

The linear framebuffer address is already provided by GRUB via multiboot2; we just change the resolution and the kernel's `SCREEN_W/H/PITCH` variables.

- [ ] **Step 1: Add vesa API to kernel.h**

Add after the signal section:

```c
/* -- BGA / VESA mode switching -------------------------------------------- */
typedef struct {
    u16 w, h;
    const char *label;
} vesa_mode_t;

void        vesa_init(void);
int         vesa_set_mode(u16 w, u16 h);
u32         vesa_mode_count(void);
vesa_mode_t vesa_mode_get(u32 idx);
u16         vesa_current_w(void);
u16         vesa_current_h(void);
```

- [ ] **Step 2: Add vesa_mode to careos_settings_t in kernel.h**

In the `careos_settings_t` struct, add:

```c
    u32 vesa_w;
    u32 vesa_h;
```

Add corresponding setter prototype:

```c
void settings_set_vesa_mode(u32 w, u32 h);
```

- [ ] **Step 3: Implement settings_set_vesa_mode in kernel/settings.c**

Read the existing settings functions pattern in `kernel/settings.c`. Add:

```c
void settings_set_vesa_mode(u32 w, u32 h) {
    g_settings.vesa_w = w;
    g_settings.vesa_h = h;
    settings_save();
}
```

- [ ] **Step 4: Write drivers/vesa.c**

```c
/* CareOS v9 -- drivers/vesa.c
 * Bochs Graphics Adapter (BGA) mode switching.
 * QEMU -vga std exposes BGA at I/O ports 0x01CE/0x01CF.
 * Works natively in 64-bit long mode — no real-mode trampoline needed.
 */
#include "kernel.h"

#define BGA_INDEX  0x01CE
#define BGA_DATA   0x01CF

#define BGA_REG_ID         0
#define BGA_REG_XRES       1
#define BGA_REG_YRES       2
#define BGA_REG_BPP        3
#define BGA_REG_ENABLE     4
#define BGA_REG_VIRT_W     6
#define BGA_REG_VIRT_H     7

#define BGA_ENABLE_LFB     0x41   /* enable + use linear framebuffer */
#define BGA_DISABLE        0x00

static u16 g_cur_w = 0, g_cur_h = 0;
static bool g_bga_present = false;

static void bga_write(u16 reg, u16 val) {
    outw(BGA_INDEX, reg);
    outw(BGA_DATA,  val);
}

static u16 bga_read(u16 reg) {
    outw(BGA_INDEX, reg);
    return inw(BGA_DATA);
}

static const vesa_mode_t g_modes[] = {
    {  800,  600, " 800x600"  },
    { 1024,  768, "1024x768"  },
    { 1280,  720, "1280x720"  },
    { 1280, 1024, "1280x1024" },
    { 1366,  768, "1366x768"  },
    { 1600,  900, "1600x900"  },
    { 1920, 1080, "1920x1080" },
    { 2560, 1440, "2560x1440" },
};
#define G_MODE_COUNT (sizeof(g_modes)/sizeof(g_modes[0]))

void vesa_init(void) {
    u16 id = bga_read(BGA_REG_ID);
    if (id < 0xB0C0 || id > 0xB0C5) {
        serial_write("[VESA] BGA not detected (id=");
        char b[8]; kutoa(id, b, 16); serial_write(b);
        serial_write("), running at GRUB resolution\n");
        g_bga_present = false;
        /* Use whatever GRUB gave us */
        g_cur_w = (u16)SCREEN_W;
        g_cur_h = (u16)SCREEN_H;
        return;
    }
    g_bga_present = true;
    g_cur_w = (u16)SCREEN_W;
    g_cur_h = (u16)SCREEN_H;
    serial_write("[VESA] BGA detected, id=0xB0C");
    char b[4]; kutoa(id - 0xB0C0, b, 10); serial_write(b); serial_write("\n");

    /* Apply saved preference */
    const careos_settings_t *s = settings_get();
    if (s->vesa_w >= 800 && s->vesa_h >= 600) {
        vesa_set_mode((u16)s->vesa_w, (u16)s->vesa_h);
    }
}

int vesa_set_mode(u16 w, u16 h) {
    if (!g_bga_present) return -1;
    bga_write(BGA_REG_ENABLE, BGA_DISABLE);
    bga_write(BGA_REG_XRES,   w);
    bga_write(BGA_REG_YRES,   h);
    bga_write(BGA_REG_BPP,    32);
    bga_write(BGA_REG_VIRT_W, w);
    bga_write(BGA_REG_VIRT_H, h);
    bga_write(BGA_REG_ENABLE, BGA_ENABLE_LFB);

    /* Verify */
    u16 rw = bga_read(BGA_REG_XRES);
    u16 rh = bga_read(BGA_REG_YRES);
    if (rw != w || rh != h) {
        serial_write("[VESA] mode set failed\n");
        return -1;
    }

    g_cur_w = w; g_cur_h = h;
    SCREEN_W     = w;
    SCREEN_H     = h;
    SCREEN_PITCH = (u32)w * 4;
    /* FRAMEBUFFER pointer stays the same — BGA keeps linear fb at same addr */
    gfx_init(FRAMEBUFFER, w, h, SCREEN_PITCH);

    serial_write("[VESA] mode ");
    char bw[8], bh[8]; kutoa(w,bw,10); kutoa(h,bh,10);
    serial_write(bw); serial_write("x"); serial_write(bh); serial_write(" set\n");
    return 0;
}

u32         vesa_mode_count(void)        { return G_MODE_COUNT; }
vesa_mode_t vesa_mode_get(u32 idx)      { return g_modes[idx < G_MODE_COUNT ? idx : 0]; }
u16         vesa_current_w(void)         { return g_cur_w; }
u16         vesa_current_h(void)         { return g_cur_h; }
```

- [ ] **Step 5: Call vesa_init in kernel/kernel.c**

In `kernel_main()`, after `paging_init(); slog_ok("Paging");` add:

```c
    vesa_init();          slog_ok("VESA/BGA");
```

- [ ] **Step 6: Add drivers/vesa.c to Makefile**

Add `drivers/vesa.c \` after `drivers/pci.c \` in C_SRC.

- [ ] **Step 7: Expose SCREEN_W/H/PITCH as extern in gui.h**

In `gui/gui.h`, verify `SCREEN_W`, `SCREEN_H`, `SCREEN_PITCH`, `FRAMEBUFFER` are already declared `extern`. They are — confirm and move on.

- [ ] **Step 8: Build and verify**

```bash
make clean && make 2>&1 | tail -20
```

- [ ] **Step 9: Commit**

```bash
git add drivers/vesa.c include/kernel.h kernel/settings.c kernel/kernel.c Makefile
git commit -m "feat: BGA graphics mode switching (QEMU vga std compatible)"
```

---

## Task 5: Settings — Display Tab

**Files:**
- Modify: `apps/app_settings.c`

Read the current `app_settings.c` to understand the tab layout, then add a Display tab.

- [ ] **Step 1: Read current settings app structure**

Read `apps/app_settings.c` lines 1–60 to understand tab numbering and draw structure.

- [ ] **Step 2: Add Display tab to app_settings_draw**

In `app_settings_draw()`, the existing tabs are numbered (e.g. 0=General, 1=Appearance, etc.). Add a new tab (e.g. tab N = "Display"). In the tab strip render block, append:

```c
    /* Display tab label — append after existing tabs */
    { "Display", COL_ORANGE }
```

In the tab content draw block, add a new `else if (w->settings_tab == N)` branch:

```c
    else if (w->settings_tab == SETTINGS_TAB_DISPLAY) {
        i32 cy = cr.y + 16;
        char cur[32];
        ksprintf(cur, "Current: %ux%u", (u32)vesa_current_w(), (u32)vesa_current_h());
        gfx_str(cr.x + 16, cy, cur, COL_TEXT, COL_TRANSPARENT);
        cy += FH * 2 + 8;

        u32 mcnt = vesa_mode_count();
        for (u32 mi = 0; mi < mcnt; mi++) {
            vesa_mode_t vm = vesa_mode_get(mi);
            bool sel = (vm.w == vesa_current_w() && vm.h == vesa_current_h());
            u32 bg = sel ? COL_PRIMARY : COL_SURFACE2;
            u32 fg = sel ? COL_WHITE   : COL_TEXT;
            gfx_rect_rounded(cr.x + 16, cy, cr.w - 32, FH + 12, 6, bg);
            if (!sel) gfx_rect_rounded_outline(cr.x + 16, cy, cr.w - 32, FH + 12, 6, COL_BORDER);
            gfx_str(cr.x + 28, cy + 6, vm.label, fg, COL_TRANSPARENT);
            cy += FH + 18;
        }
    }
```

- [ ] **Step 3: Add Display tab click handling in app_settings_click**

In the click handler for the Display tab content area, detect clicks on mode rows:

```c
    else if (w->settings_tab == SETTINGS_TAB_DISPLAY) {
        i32 cy = cr.y + 16 + FH * 2 + 8;
        u32 mcnt = vesa_mode_count();
        for (u32 mi = 0; mi < mcnt; mi++) {
            if (my >= cy && my < cy + FH + 12 && mx >= cr.x + 16 && mx < cr.x + cr.w - 16) {
                vesa_mode_t vm = vesa_mode_get(mi);
                if (vesa_set_mode(vm.w, vm.h) == 0) {
                    settings_set_vesa_mode(vm.w, vm.h);
                    notify_push("Display", "Resolution changed", COL_GREEN);
                } else {
                    notify_push("Display", "Mode not supported", COL_RED);
                }
                return;
            }
            cy += FH + 18;
        }
    }
```

- [ ] **Step 4: Build and verify**

```bash
make clean && make 2>&1 | tail -20
```

- [ ] **Step 5: Commit**

```bash
git add apps/app_settings.c
git commit -m "feat: Display tab in Settings with BGA resolution picker"
```

---

## Task 6: File Manager — Copy/Paste, Rename, Drag, Preview

**Files:**
- Modify: `gui/gui.h` (clip_mode_t, g_clip_mode, g_clip_path, drag_file_t; fm_renaming + fm_rename_input in window_t)
- Modify: `gui/wm.c` (drag_file_t globals, ghost render in wm_handle_mouse)
- Modify: `apps/app_files.c` (copy/paste, rename, drag initiation, preview pane)

- [ ] **Step 1: Add fields to gui.h**

In `gui/gui.h`, after the `/* -- Clipboard */` section add:

```c
typedef enum { CLIP_NONE, CLIP_FILE_COPY, CLIP_FILE_CUT } clip_mode_t;
extern clip_mode_t g_clip_mode;
extern char        g_clip_path[FS_PATH_MAX];
```

In the `window_t` struct, after the `fm_sel` field add:

```c
    bool        fm_renaming;
    char        fm_rename_buf[FS_NAME_MAX];
    u32         fm_rename_len;
```

In `window_t`, after `fm_rename_len` add:

```c
    bool        fm_drag_active;
    i32         fm_drag_start_x, fm_drag_start_y;
```

After the widget/wm section add:

```c
/* -- File drag state (global, one drag at a time) ------------------------- */
typedef struct {
    bool       active;
    char       path[FS_PATH_MAX];
    char       name[FS_NAME_MAX];
    window_t  *src_win;
} drag_file_t;
extern drag_file_t g_drag_file;
```

- [ ] **Step 2: Define globals in gui/wm.c**

At the top of `gui/wm.c` (after existing static declarations), add:

```c
clip_mode_t g_clip_mode  = CLIP_NONE;
char        g_clip_path[FS_PATH_MAX] = {0};
drag_file_t g_drag_file  = {0};
```

- [ ] **Step 3: Draw drag ghost in wm_handle_mouse in wm.c**

In `wm_handle_mouse()`, after all normal mouse dispatch, add at the very end before returning:

```c
    /* Draw file drag ghost */
    if (g_drag_file.active) {
        gfx_rect_rounded(m->x + 8, m->y - 10, 120, 20, 4, COL_SURFACE3);
        gfx_rect_rounded_outline(m->x + 8, m->y - 10, 120, 20, 4, COL_BORDER);
        gfx_str_clipped(m->x + 14, m->y - 10 + 4, 108, g_drag_file.name, COL_TEXT, COL_TRANSPARENT);

        /* On release: drop into target Files window */
        if (m->left_released) {
            window_t *target = NULL;
            for (int i = 0; i < MAX_WINDOWS; i++) {
                window_t *tw = wm_get_window(i);
                if (!tw || !tw->active || tw->app != APP_FILES) continue;
                if (tw == g_drag_file.src_win) continue;
                rect_t tcr = wm_client_rect(tw);
                if (rect_contains(tcr, m->x, m->y)) { target = tw; break; }
            }
            if (target && target->fm_dir) {
                fs_node_t *src = vfs_resolve_path(g_drag_file.path);
                if (src) vfs_copy(src, target->fm_dir, src->name);
                notify_push("Files", "File copied", COL_GREEN);
            }
            g_drag_file.active = false;
        }
    }
```

- [ ] **Step 4: Rewrite app_files.c — copy/paste, rename, drag initiation, preview pane**

Read the full `apps/app_files.c` to understand the current layout. Then add:

**In `app_files_key()`** — add clipboard and rename handling:

```c
void app_files_key(window_t *w, char c) {
    if (w->tab == FM_MODE_RENAME) {
        if (c == '\n') {
            /* Commit rename */
            if (w->fm_rename_len > 0 && w->fm_dir && w->fm_sel < w->fm_dir->child_count) {
                fs_node_t *node = w->fm_dir->children[w->fm_sel];
                w->fm_rename_buf[w->fm_rename_len] = '\0';
                vfs_rename(node, w->fm_rename_buf);
            }
            w->tab = FM_MODE_BROWSE;
        } else if (c == 27) {
            w->tab = FM_MODE_BROWSE;
        } else if (c == '\b') {
            if (w->fm_rename_len > 0) w->fm_rename_buf[--w->fm_rename_len] = '\0';
        } else if (c >= 32 && c < 127 && w->fm_rename_len < FS_NAME_MAX - 1) {
            w->fm_rename_buf[w->fm_rename_len++] = c;
            w->fm_rename_buf[w->fm_rename_len]   = '\0';
        }
        return;
    }

    /* F2 = rename */
    /* Note: F2 is scancode 0x3C; keyboard driver doesn't map it yet.
       Use 'r' as rename shortcut for now */
    if (c == 'r' || c == 'R') {
        if (w->fm_dir && w->fm_sel < w->fm_dir->child_count) {
            fs_node_t *node = w->fm_dir->children[w->fm_sel];
            kstrncpy(w->fm_rename_buf, node->name, FS_NAME_MAX - 1);
            w->fm_rename_len = (u32)kstrlen(w->fm_rename_buf);
            w->tab = FM_MODE_RENAME;
        }
        return;
    }

    /* Ctrl+C = copy, Ctrl+X = cut, Ctrl+V = paste */
    extern bool keyboard_ctrl_held(void);
    if (keyboard_ctrl_held()) {
        if (c == 3) { /* Ctrl+C */
            if (w->fm_dir && w->fm_sel < w->fm_dir->child_count) {
                fs_node_t *node = w->fm_dir->children[w->fm_sel];
                vfs_get_path(node, g_clip_path, FS_PATH_MAX);
                g_clip_mode = CLIP_FILE_COPY;
                notify_push("Files", "Copied", COL_GREEN);
            }
            return;
        }
        if (c == 24) { /* Ctrl+X */
            if (w->fm_dir && w->fm_sel < w->fm_dir->child_count) {
                fs_node_t *node = w->fm_dir->children[w->fm_sel];
                vfs_get_path(node, g_clip_path, FS_PATH_MAX);
                g_clip_mode = CLIP_FILE_CUT;
                notify_push("Files", "Cut", COL_YELLOW);
            }
            return;
        }
        if (c == 22) { /* Ctrl+V */
            if (g_clip_mode != CLIP_NONE && g_clip_path[0] && w->fm_dir) {
                fs_node_t *src = vfs_resolve_path(g_clip_path);
                if (src) {
                    if (g_clip_mode == CLIP_FILE_COPY) {
                        vfs_copy(src, w->fm_dir, src->name);
                        notify_push("Files", "Pasted (copy)", COL_GREEN);
                    } else {
                        vfs_rename(src, src->name); /* move: reparent */
                        notify_push("Files", "Pasted (move)", COL_GREEN);
                        g_clip_mode = CLIP_NONE;
                    }
                }
            }
            return;
        }
    }

    /* Existing navigation (Up/Down arrow etc.) — keep existing logic */
    /* ... existing key handling ... */
}
```

**Preview pane in `app_files_draw()`** — add after the file list is drawn, when `cr.w > 500`:

```c
    /* Preview pane */
    if (cr.w > 500 && w->fm_dir && w->fm_sel < w->fm_dir->child_count) {
        i32 prev_x = cr.x + cr.w * 2 / 3;
        i32 prev_w = cr.w / 3 - 4;
        gfx_vline(prev_x, cr.y + FM_TB, cr.h - FM_TB, COL_BORDER);
        fs_node_t *sel = w->fm_dir->children[w->fm_sel];
        i32 py = cr.y + FM_TB + 8;
        gfx_str_clipped(prev_x + 6, py, prev_w - 12, sel->name, COL_ACCENT, COL_TRANSPARENT);
        py += FONT_H * (i32)GFX_FONT_SCALE + 6;
        if (sel->type == FS_DIR) {
            char tmp[32]; ksprintf(tmp, "[Directory]  %u items", sel->child_count);
            gfx_str_clipped(prev_x + 6, py, prev_w - 12, tmp, COL_DIM, COL_TRANSPARENT);
        } else {
            char sz[32]; ksprintf(sz, "%u bytes", sel->size);
            gfx_str_clipped(prev_x + 6, py, prev_w - 12, sz, COL_DIM, COL_TRANSPARENT);
            py += FONT_H * (i32)GFX_FONT_SCALE + 4;
            /* Text preview */
            bool is_text = false;
            const char *exts[] = {".txt",".c",".h",".sh",".md",".cfg",".log"};
            for (int ei = 0; ei < 7; ei++) {
                u32 nl = (u32)kstrlen(sel->name), el = (u32)kstrlen(exts[ei]);
                if (nl > el && kstrcmp(sel->name + nl - el, exts[ei]) == 0) { is_text = true; break; }
            }
            if (is_text && sel->size > 0) {
                char line[80]; u32 li = 0, lines = 0;
                for (u32 ci = 0; ci <= sel->size && lines < 8; ci++) {
                    char ch = ci < sel->size ? sel->data[ci] : '\n';
                    if (ch == '\n' || li >= 79) {
                        line[li] = '\0'; li = 0; lines++;
                        gfx_str_clipped(prev_x + 6, py, prev_w - 12, line, COL_TEXT, COL_TRANSPARENT);
                        py += FONT_H * (i32)GFX_FONT_SCALE + 2;
                    } else { line[li++] = ch; }
                }
            } else if (!is_text) {
                gfx_str_clipped(prev_x + 6, py, prev_w - 12, "[Binary file]", COL_MUTED, COL_TRANSPARENT);
            }
        }
    }

    /* Rename overlay */
    if (w->tab == FM_MODE_RENAME) {
        /* Draw input box over selected row */
        i32 lx = cr.x + FM_SB + 2;
        i32 row_h = FM_ROW;
        i32 sel_y = cr.y + FM_TB + 2 + (i32)w->fm_sel * (row_h + 2) - (i32)w->scroll * (row_h + 2);
        gfx_rect_rounded(lx, sel_y, cr.w - FM_SB - 4, row_h, 4, COL_INPUT_BG);
        gfx_rect_rounded_outline(lx, sel_y, cr.w - FM_SB - 4, row_h, 4, COL_PRIMARY);
        gfx_str_clipped(lx + 4, sel_y + (row_h - FONT_H * (i32)GFX_FONT_SCALE) / 2,
                        cr.w - FM_SB - 12, w->fm_rename_buf, COL_TEXT, COL_TRANSPARENT);
    }
```

- [ ] **Step 5: Build and verify**

```bash
make clean && make 2>&1 | tail -20
```

- [ ] **Step 6: Commit**

```bash
git add gui/gui.h gui/wm.c apps/app_files.c
git commit -m "feat: file manager copy/paste, rename, drag-and-drop, preview pane"
```

---

## Task 7: Code Editor — Search/Replace + Open/Save Dialog

The editor already has line numbers and basic syntax highlighting. This task adds search/replace and a file open/save dialog.

**Files:**
- Modify: `gui/gui.h` (editor find/replace/dialog fields in window_t)
- Modify: `apps/app_editor.c` (find bar, replace bar, dialog overlay, better highlight)

- [ ] **Step 1: Add editor fields to window_t in gui.h**

In `window_t`, after `editor_modified` add:

```c
    /* Editor search/replace */
    char  editor_find_buf[128];
    u32   editor_find_len;
    char  editor_replace_buf[128];
    u32   editor_replace_len;
    bool  editor_find_active;
    bool  editor_replace_active;
    u32   editor_match_pos;     /* byte offset of current match in text_buf */
    u32   editor_match_count;

    /* Editor file dialog */
    bool       editor_dialog_open;   /* true = open, false = save */
    bool       editor_dialog_active;
    fs_node_t *editor_dialog_dir;
    u32        editor_dialog_sel;
    char       editor_dialog_name[FS_NAME_MAX];
    u32        editor_dialog_name_len;
```

- [ ] **Step 2: Rewrite app_editor_draw to add find bar and dialog**

In `apps/app_editor.c`, extend `app_editor_draw()`. After the existing draw logic at the end, before `gfx_clear_clip()` (or wherever the function ends), add:

```c
    /* Find bar */
    if (w->editor_find_active) {
        i32 bar_h = 28;
        i32 bar_y = cr.y + cr.h - sb_h - bar_h;
        gfx_rect(cr.x, bar_y, cr.w, bar_h, COL_SURFACE2);
        gfx_hline(cr.x, bar_y, cr.w, COL_BORDER);
        gfx_str(cr.x + 8, bar_y + (bar_h - FONT_H * sc) / 2, "Find:", COL_DIM, COL_TRANSPARENT);
        i32 fi_x = cr.x + 52, fi_w = cr.w - 200;
        gfx_rect_rounded(fi_x, bar_y + 4, fi_w, bar_h - 8, 4, COL_INPUT_BG);
        gfx_rect_rounded_outline(fi_x, bar_y + 4, fi_w, bar_h - 8, 4, COL_PRIMARY);
        gfx_str_clipped(fi_x + 4, bar_y + (bar_h - FONT_H * sc) / 2, fi_w - 8,
                        w->editor_find_buf, COL_TEXT, COL_TRANSPARENT);
        char cnt[32]; ksprintf(cnt, "%u matches", w->editor_match_count);
        gfx_str(fi_x + fi_w + 8, bar_y + (bar_h - FONT_H * sc) / 2, cnt, COL_DIM, COL_TRANSPARENT);
    }

    /* File open/save dialog overlay */
    if (w->editor_dialog_active) {
        rect_t cr2 = wm_client_rect(w);
        gfx_rect_alpha(cr2.x, cr2.y, cr2.w, cr2.h, COL_BG, 220);
        i32 dw = cr2.w - 80, dh = cr2.h - 80;
        i32 dx = cr2.x + 40, dy = cr2.y + 40;
        gfx_rect_rounded(dx, dy, dw, dh, 8, COL_SURFACE);
        gfx_rect_rounded_outline(dx, dy, dw, dh, 8, COL_BORDER);
        gfx_str(dx + 12, dy + 10, w->editor_dialog_open ? "Open File" : "Save File",
                COL_TEXT, COL_TRANSPARENT);
        gfx_hline(dx, dy + 28, dw, COL_BORDER);

        /* File list */
        if (w->editor_dialog_dir) {
            u32 lh = (u32)(FONT_H * sc) + 4;
            for (u32 i = 0; i < w->editor_dialog_dir->child_count && (i32)(dy + 32 + i * lh) < dy + dh - 48; i++) {
                fs_node_t *child = w->editor_dialog_dir->children[i];
                bool sel = (i == w->editor_dialog_sel);
                if (sel) gfx_rect(dx + 4, dy + 32 + (i32)(i * lh), dw - 8, (i32)lh, COL_SELECTION);
                u32 fg = child->type == FS_DIR ? COL_ACCENT : COL_TEXT;
                char label[80]; ksprintf(label, "%s%s", child->name, child->type==FS_DIR?"/":"");
                gfx_str_clipped(dx + 8, dy + 32 + (i32)(i * lh) + 2, dw - 16, label, fg, COL_TRANSPARENT);
            }
        }

        /* Filename input */
        i32 ni_y = dy + dh - 42;
        gfx_hline(dx, ni_y - 4, dw, COL_BORDER);
        gfx_str(dx + 8, ni_y + 4, "Name:", COL_DIM, COL_TRANSPARENT);
        gfx_rect_rounded(dx + 52, ni_y, dw - 80, 28, 4, COL_INPUT_BG);
        gfx_rect_rounded_outline(dx + 52, ni_y, dw - 80, 28, 4, COL_PRIMARY);
        gfx_str_clipped(dx + 58, ni_y + 6, dw - 90, w->editor_dialog_name, COL_TEXT, COL_TRANSPARENT);
        /* OK button */
        gfx_rect_rounded(dx + dw - 56, ni_y, 48, 28, 6, COL_PRIMARY);
        gfx_str_centered(dx + dw - 56, ni_y + 6, 48, "OK", COL_WHITE, COL_TRANSPARENT);
    }
```

- [ ] **Step 3: Add search logic to app_editor_key**

In `app_editor_key()`, add handling for Ctrl+F, Ctrl+H, Ctrl+O, Ctrl+S, Escape:

```c
void app_editor_key(window_t *w, char c) {
    /* Dialog mode */
    if (w->editor_dialog_active) {
        if (c == 27) { w->editor_dialog_active = false; return; }
        if (c == '\n') {
            /* Commit open or save */
            if (w->editor_dialog_open) {
                /* Open: find file by name in dialog dir */
                if (w->editor_dialog_dir && w->editor_dialog_name[0]) {
                    fs_node_t *f = vfs_find(w->editor_dialog_dir, w->editor_dialog_name);
                    if (f && f->type == FS_FILE) {
                        win_clear(w);
                        vfs_read(f, w->text_buf, WIN_TEXT_BUF - 1);
                        w->text_len = (u32)kstrlen(w->text_buf);
                        vfs_get_path(f, w->editor_path, FS_PATH_MAX);
                        w->editor_modified = false;
                    }
                }
            } else {
                /* Save */
                if (w->editor_dialog_name[0]) {
                    fs_node_t *f = vfs_find(w->editor_dialog_dir, w->editor_dialog_name);
                    if (!f) f = vfs_mkfile(w->editor_dialog_dir, w->editor_dialog_name);
                    if (f) {
                        vfs_write(f, w->text_buf, w->text_len);
                        vfs_get_path(f, w->editor_path, FS_PATH_MAX);
                        w->editor_modified = false;
                    }
                }
            }
            w->editor_dialog_active = false;
            return;
        }
        if (c == '\b' && w->editor_dialog_name_len > 0)
            w->editor_dialog_name[--w->editor_dialog_name_len] = '\0';
        else if (c >= 32 && c < 127 && w->editor_dialog_name_len < FS_NAME_MAX - 1) {
            w->editor_dialog_name[w->editor_dialog_name_len++] = c;
            w->editor_dialog_name[w->editor_dialog_name_len]   = '\0';
        }
        return;
    }

    /* Find bar mode */
    if (w->editor_find_active) {
        if (c == 27) { w->editor_find_active = false; w->editor_replace_active = false; return; }
        if (c == '\n') {
            /* Next match */
            if (w->editor_match_count > 0) {
                u32 pat_len = (u32)kstrlen(w->editor_find_buf);
                u32 start = w->editor_match_pos + 1;
                bool found = false;
                for (u32 i = start; i + pat_len <= w->text_len; i++) {
                    if (kstrncmp(w->text_buf + i, w->editor_find_buf, pat_len) == 0) {
                        w->editor_match_pos = i; found = true; break;
                    }
                }
                if (!found) {
                    for (u32 i = 0; i + pat_len <= w->text_len; i++) {
                        if (kstrncmp(w->text_buf + i, w->editor_find_buf, pat_len) == 0) {
                            w->editor_match_pos = i; break;
                        }
                    }
                }
            }
            return;
        }
        if (c == '\b' && w->editor_find_len > 0)
            w->editor_find_buf[--w->editor_find_len] = '\0';
        else if (c >= 32 && c < 127 && w->editor_find_len < 127) {
            w->editor_find_buf[w->editor_find_len++] = c;
            w->editor_find_buf[w->editor_find_len]   = '\0';
        }
        /* Recount matches */
        w->editor_match_count = 0;
        u32 pl = (u32)kstrlen(w->editor_find_buf);
        if (pl > 0) {
            for (u32 i = 0; i + pl <= w->text_len; i++) {
                if (kstrncmp(w->text_buf + i, w->editor_find_buf, pl) == 0)
                    w->editor_match_count++;
            }
        }
        return;
    }

    /* Global shortcuts */
    extern bool keyboard_ctrl_held(void);
    if (keyboard_ctrl_held()) {
        if (c == 6) { /* Ctrl+F */
            w->editor_find_active = true;
            w->editor_find_buf[0] = '\0'; w->editor_find_len = 0;
            w->editor_match_count = 0;
            return;
        }
        if (c == 15) { /* Ctrl+O */
            w->editor_dialog_active = true;
            w->editor_dialog_open   = true;
            w->editor_dialog_dir    = vfs_root();
            w->editor_dialog_sel    = 0;
            w->editor_dialog_name[0] = '\0';
            w->editor_dialog_name_len = 0;
            return;
        }
        if (c == 19) { /* Ctrl+S */
            if (w->editor_path[0]) {
                /* Save to existing path */
                fs_node_t *f = vfs_resolve_path(w->editor_path);
                if (!f) {
                    /* Resolve parent and create */
                    f = vfs_mkfile(vfs_root(), "untitled.c");
                }
                if (f) { vfs_write(f, w->text_buf, w->text_len); w->editor_modified = false; }
            } else {
                /* Save-as dialog */
                w->editor_dialog_active = true;
                w->editor_dialog_open   = false;
                w->editor_dialog_dir    = vfs_root();
                w->editor_dialog_sel    = 0;
                w->editor_dialog_name[0] = '\0';
                w->editor_dialog_name_len = 0;
            }
            return;
        }
    }

    /* Existing key handling (keep existing logic) */
    /* ... existing app_editor_key body ... */
}
```

- [ ] **Step 4: Build and verify**

```bash
make clean && make 2>&1 | tail -20
```

- [ ] **Step 5: Commit**

```bash
git add gui/gui.h apps/app_editor.c
git commit -m "feat: editor search/replace, open/save dialog"
```

---

## Task 8: Package Manager — Real Install/Remove + Deps

**Files:**
- Modify: `kernel/carepkg.c` (install/remove write to VFS, deps, /var/pkg/installed.db)
- Modify: `apps/app_pkgmgr.c` (tabs, detail pane, progress indicator)
- Modify: `kernel/kernel.c` (create /usr/bin, /var/pkg at boot)

- [ ] **Step 1: Read current carepkg.c structure**

Read `kernel/carepkg.c` lines 1–80 to understand the package catalog structure.

- [ ] **Step 2: Extend package catalog struct in carepkg.c**

The catalog entries currently have name, version, description. Add `deps` and `files` fields:

```c
typedef struct {
    const char *name;
    const char *version;
    const char *description;
    const char *category;
    const char *deps;           /* comma-separated dep names, "" if none */
    const char *install_path;   /* VFS path for main file, e.g. "/usr/bin/hello" */
    const char *install_data;   /* content to write, NULL if no file to install */
    bool installed;
} pkg_entry_t;
```

Update existing catalog entries to add the new fields (empty `deps=""`, representative install_path and a small install_data string for packages that make sense):

```c
static pkg_entry_t g_catalog[] = {
    { "hello",   "1.0", "Hello World program",  "utils",   "",      "/usr/bin/hello",   "#!/bin/sh\necho Hello, CareOS!\n", false },
    { "fortune",  "1.0", "Fortune cookie",       "utils",   "",      "/usr/bin/fortune", "#!/bin/sh\necho Have a nice day!\n", false },
    { "sysutils", "1.0", "System utilities",     "system",  "",      "/usr/bin/sysutils","# sysutils placeholder\n", false },
    /* existing entries … keep and extend */
};
```

- [ ] **Step 3: Add install/remove/DB logic to carepkg.c**

Add at the top of carepkg.c (after includes):

```c
#define INSTALLED_DB_PATH "/var/pkg/installed.db"
#define VISITED_MAX 8

static void db_load(void) {
    fs_node_t *f = vfs_resolve_path(INSTALLED_DB_PATH);
    if (!f) return;
    char buf[2048]; vfs_read(f, buf, sizeof(buf) - 1); buf[2047] = '\0';
    char *line = buf;
    while (*line) {
        char *nl = kstrchr(line, '\n');
        if (nl) *nl = '\0';
        /* format: name|version|files */
        char *bar = kstrchr(line, '|');
        if (bar) { *bar = '\0'; }
        /* Mark catalog entry installed */
        for (u32 i = 0; i < g_catalog_count; i++) {
            if (kstrcmp(g_catalog[i].name, line) == 0) {
                g_catalog[i].installed = true; break;
            }
        }
        if (nl) line = nl + 1; else break;
    }
}

static void db_save(void) {
    char buf[2048]; buf[0] = '\0';
    u32 pos = 0;
    for (u32 i = 0; i < g_catalog_count; i++) {
        if (!g_catalog[i].installed) continue;
        u32 l = (u32)kstrlen(g_catalog[i].name);
        if (pos + l + 2 < 2047) {
            kmemcpy(buf + pos, g_catalog[i].name, l); pos += l;
            buf[pos++] = '\n';
        }
    }
    buf[pos] = '\0';
    fs_node_t *f = vfs_resolve_path(INSTALLED_DB_PATH);
    if (!f) {
        fs_node_t *dir = vfs_resolve_path("/var/pkg");
        if (!dir) {
            fs_node_t *var = vfs_find(vfs_root(), "var");
            if (!var) var = vfs_mkdir(vfs_root(), "var");
            dir = var ? vfs_mkdir(var, "pkg") : NULL;
        }
        if (dir) f = vfs_mkfile(dir, "installed.db");
    }
    if (f) vfs_write(f, buf, pos);
}

static int install_pkg(const char *name, char visited[VISITED_MAX][32], int depth);

static int install_pkg(const char *name, char visited[VISITED_MAX][32], int depth) {
    if (depth >= VISITED_MAX) return -1;
    /* Check already installed */
    for (u32 i = 0; i < g_catalog_count; i++) {
        if (kstrcmp(g_catalog[i].name, name) == 0 && g_catalog[i].installed) return 0;
    }
    /* Find in catalog */
    int idx = -1;
    for (u32 i = 0; i < g_catalog_count; i++) {
        if (kstrcmp(g_catalog[i].name, name) == 0) { idx = (int)i; break; }
    }
    if (idx < 0) return -1;

    /* Install deps first */
    if (g_catalog[idx].deps && g_catalog[idx].deps[0]) {
        char dep_buf[128]; kstrncpy(dep_buf, g_catalog[idx].deps, 127);
        char *tok = dep_buf;
        while (tok && *tok) {
            char *comma = kstrchr(tok, ',');
            if (comma) *comma = '\0';
            kstrncpy(visited[depth], tok, 31);
            install_pkg(tok, visited, depth + 1);
            tok = comma ? comma + 1 : NULL;
        }
    }

    /* Write install file */
    if (g_catalog[idx].install_path && g_catalog[idx].install_data) {
        /* Ensure parent dir exists */
        fs_node_t *usr = vfs_find(vfs_root(), "usr");
        if (!usr) usr = vfs_mkdir(vfs_root(), "usr");
        fs_node_t *bin = usr ? vfs_find(usr, "bin") : NULL;
        if (!bin && usr) bin = vfs_mkdir(usr, "bin");
        if (bin) {
            /* Extract filename from path */
            const char *fname = kstrrchr(g_catalog[idx].install_path, '/');
            fname = fname ? fname + 1 : g_catalog[idx].install_path;
            fs_node_t *f = vfs_find(bin, fname);
            if (!f) f = vfs_mkfile(bin, fname);
            if (f) {
                u32 dlen = (u32)kstrlen(g_catalog[idx].install_data);
                vfs_write(f, g_catalog[idx].install_data, dlen);
            }
        }
    }

    g_catalog[idx].installed = true;
    db_save();
    return 0;
}
```

Update `carepkg_install()` to call `install_pkg()`:

```c
int carepkg_install(const char *pkg_path) {
    char visited[VISITED_MAX][32];
    kmemset(visited, 0, sizeof(visited));
    return install_pkg(pkg_path, visited, 0);
}
```

Update `carepkg_remove()`:

```c
int carepkg_remove(const char *name) {
    for (u32 i = 0; i < g_catalog_count; i++) {
        if (kstrcmp(g_catalog[i].name, name) == 0) {
            /* Delete installed file */
            if (g_catalog[i].install_path) {
                fs_node_t *f = vfs_resolve_path(g_catalog[i].install_path);
                if (f) vfs_delete(f);
            }
            g_catalog[i].installed = false;
            db_save();
            return 0;
        }
    }
    return -1;
}
```

Update `carepkg_init()` to call `db_load()`:

```c
void carepkg_init(void) {
    db_load();
}
```

- [ ] **Step 4: Add /usr/bin and /var/pkg creation in kernel.c**

In `kernel_main()`, after `vfs_init()`, add:

```c
    /* Ensure standard VFS directories exist */
    {
        fs_node_t *usr = vfs_find(vfs_root(), "usr");
        if (!usr) usr = vfs_mkdir(vfs_root(), "usr");
        if (usr) { fs_node_t *bin = vfs_find(usr, "bin"); if (!bin) vfs_mkdir(usr, "bin"); }
        fs_node_t *var = vfs_find(vfs_root(), "var");
        if (!var) var = vfs_mkdir(vfs_root(), "var");
        if (var) { fs_node_t *pkg = vfs_find(var, "pkg"); if (!pkg) vfs_mkdir(var, "pkg"); }
    }
```

- [ ] **Step 5: Add Installed tab + detail pane to app_pkgmgr.c**

Read `apps/app_pkgmgr.c` first, then add:
- A third tab "Installed" that filters `carepkg_is_installed(name) == true`
- Below the package list, a detail pane showing description, version, deps of selected package
- During install, show a progress-style animated dots message in the status area using `(timer_get_ticks() / 20) % 4` to cycle 0–3 dots

```c
    /* Status with animated dots during "installing" state */
    if (w->pkgmgr_installing) {
        char dots[8] = "..."; dots[(timer_get_ticks()/20)%4] = '\0';
        char msg[64]; ksprintf(msg, "Installing%s", dots);
        gfx_str(cr.x + 8, stat_y + 4, msg, COL_ACCENT, COL_TRANSPARENT);
    }
```

Add `bool pkgmgr_installing` to `window_t` in `gui/gui.h`.

- [ ] **Step 6: Build and verify**

```bash
make clean && make 2>&1 | tail -20
```

- [ ] **Step 7: Commit**

```bash
git add kernel/carepkg.c apps/app_pkgmgr.c kernel/kernel.c gui/gui.h
git commit -m "feat: package manager real install/remove, deps, VFS write, installed DB"
```

---

## Task 9: Browser — Tabs + Bookmarks

**Files:**
- Modify: `gui/gui.h` (browser_tab_t struct, browser_tabs in window_t, bookmark_t, bookmark fields)
- Modify: `apps/app_browser.c` (tab strip, tab management, bookmarks dropdown)

- [ ] **Step 1: Add browser_tab_t and bookmark_t to gui.h**

In `gui/gui.h`, before `window_t`, add:

```c
/* -- Browser tab ---------------------------------------------------------- */
#define BROWSER_TAB_CONTENT_MAX 8192

typedef struct {
    char url[256];
    char content[BROWSER_TAB_CONTENT_MAX];
    char title[128];
    i32  scroll;
    bool loading;
    bool url_active;
    char history[8][256];
    u32  history_pos;
    u32  history_count;
} browser_tab_t;

/* -- Browser bookmark ----------------------------------------------------- */
typedef struct {
    char title[64];
    char url[256];
} browser_bookmark_t;
```

In `window_t`, **replace** the existing browser fields:

```c
    /* Old:
    char  browser_url[256];
    char  browser_content[16384];
    char  browser_title[128];
    bool  browser_loading;
    bool  browser_url_active;
    i32   browser_scroll;
    u32   browser_history_pos;
    char  browser_history[10][256];
    u32   browser_history_count;
    */

    /* New: */
    browser_tab_t  browser_tabs[4];
    u32            browser_tab_count;
    u32            browser_active_tab;
    browser_bookmark_t browser_bookmarks[32];
    u32            browser_bookmark_count;
    bool           browser_bookmarks_open;  /* dropdown visible */
    bool           browser_view_source;
    /* find */
    char           browser_find_buf[128];
    u32            browser_find_len;
    bool           browser_find_active;
    u32            browser_find_matches[64];
    u32            browser_find_count;
    u32            browser_find_current;
```

- [ ] **Step 2: Add convenience macros/accessor in app_browser.c**

At the top of `apps/app_browser.c`, after the existing layout defines, add:

```c
/* Active tab accessor */
#define ATAB(w)        (&(w)->browser_tabs[(w)->browser_active_tab])
#define ATAB_URL(w)    ATAB(w)->url
#define ATAB_CONT(w)   ATAB(w)->content
#define ATAB_TITLE(w)  ATAB(w)->title
#define ATAB_SCROLL(w) ATAB(w)->scroll
```

- [ ] **Step 3: Update app_browser_init to use tabs**

Replace the existing `app_browser_init`:

```c
void app_browser_init(window_t *w) {
    w->browser_tab_count   = 1;
    w->browser_active_tab  = 0;
    w->browser_bookmarks_open = false;
    w->browser_view_source = false;
    w->browser_find_active = false;
    w->browser_find_len    = 0;
    w->browser_bookmark_count = 0;
    kstrcpy(g_status, "Ready");
    /* Init first tab */
    browser_tab_t *t = &w->browser_tabs[0];
    kmemset(t, 0, sizeof(browser_tab_t));
    kstrcpy(t->url,   "about:home");
    kstrcpy(t->title, "New Tab");
    t->url_active = true;

    /* Load bookmarks from VFS */
    char bm_path[64];
    ksprintf(bm_path, "/home/%s/.bookmarks", user_current_name());
    fs_node_t *bmf = vfs_resolve_path(bm_path);
    if (bmf) {
        char buf[2048]; vfs_read(bmf, buf, sizeof(buf)-1); buf[2047]='\0';
        char *line = buf;
        while (*line && w->browser_bookmark_count < 32) {
            char *nl = kstrchr(line, '\n');
            if (nl) *nl = '\0';
            char *bar = kstrchr(line, '|');
            if (bar) {
                *bar = '\0';
                browser_bookmark_t *bm = &w->browser_bookmarks[w->browser_bookmark_count++];
                kstrncpy(bm->title, line, 63);
                kstrncpy(bm->url,   bar+1, 255);
            }
            if (nl) line = nl+1; else break;
        }
    }
}
```

- [ ] **Step 4: Update app_browser_navigate to use active tab**

Replace all references to `w->browser_url`, `w->browser_content`, `w->browser_title`, `w->browser_loading`, `w->browser_scroll`, `w->browser_history*`, `w->browser_url_active` in `app_browser_navigate()` with `ATAB(w)->url`, `ATAB(w)->content`, etc. Also update `history_push()` to take a tab pointer instead of window:

```c
static void history_push(browser_tab_t *t, const char *url) {
    if (t->history_count > 0 &&
        kstrcmp(t->history[t->history_count-1], url) == 0) return;
    if (t->history_count >= 8) {
        for (int i = 0; i < 7; i++) kstrncpy(t->history[i], t->history[i+1], 255);
        t->history_count = 7;
    }
    kstrncpy(t->history[t->history_count], url, 255);
    t->history_count++;
    t->history_pos = t->history_count - 1;
}
```

And change BROW_RESP_SZ still uses `g_resp` global; content copy goes to `ATAB_CONT(w)` with BROWSER_TAB_CONTENT_MAX.

- [ ] **Step 5: Draw tab strip in app_browser_draw**

In `app_browser_draw()`, before drawing the toolbar, add a 32px tab strip at the very top:

```c
    /* Tab strip */
    i32 tab_strip_h = 32;
    gfx_rect(cr.x, cr.y, cr.w, tab_strip_h, COL_SURFACE3);
    gfx_hline(cr.x, cr.y + tab_strip_h - 1, cr.w, COL_BORDER);
    i32 tab_w = (cr.w - 80) / (i32)(w->browser_tab_count < 1 ? 1 : w->browser_tab_count);
    if (tab_w > 200) tab_w = 200;
    for (u32 ti = 0; ti < w->browser_tab_count; ti++) {
        i32 tx2 = cr.x + (i32)ti * (tab_w + 2);
        bool active = (ti == w->browser_active_tab);
        u32 tbg = active ? COL_SURFACE : COL_SURFACE2;
        gfx_rect(tx2, cr.y, tab_w, tab_strip_h, tbg);
        if (active) gfx_hline(tx2, cr.y + tab_strip_h - 2, tab_w, COL_PRIMARY);
        gfx_str_clipped(tx2 + 6, cr.y + (tab_strip_h - FH)/2, tab_w - 24,
                        w->browser_tabs[ti].title, COL_TEXT, COL_TRANSPARENT);
        /* × close button */
        if (w->browser_tab_count > 1) {
            gfx_str(tx2 + tab_w - 18, cr.y + (tab_strip_h - FH)/2, "x", COL_MUTED, COL_TRANSPARENT);
        }
        /* Register as link for click detection */
        /* (handled in app_browser_click) */
    }
    /* + new tab button */
    i32 new_tab_x = cr.x + (i32)w->browser_tab_count * (tab_w + 2);
    gfx_str(new_tab_x + 6, cr.y + (tab_strip_h - FH)/2, "+", COL_DIM, COL_TRANSPARENT);

    /* Shift content area down by tab_strip_h */
    cr.y += tab_strip_h;
    cr.h -= tab_strip_h;
```

- [ ] **Step 6: Add bookmark dropdown draw**

In `app_browser_draw()`, after the toolbar section, add:

```c
    /* Bookmark dropdown */
    if (w->browser_bookmarks_open) {
        i32 bm_x = cr.x + cr.w - 160;
        i32 bm_y = cr.y;  /* below toolbar */
        i32 bm_w = 154;
        i32 row_h = FH + 10;
        gfx_rect_rounded(bm_x, bm_y, bm_w, (i32)w->browser_bookmark_count * row_h + 8, 6, COL_SURFACE2);
        gfx_rect_rounded_outline(bm_x, bm_y, bm_w, (i32)w->browser_bookmark_count * row_h + 8, 6, COL_BORDER);
        for (u32 bi = 0; bi < w->browser_bookmark_count; bi++) {
            i32 by = bm_y + 4 + (i32)bi * row_h;
            gfx_str_clipped(bm_x + 6, by + 4, bm_w - 32,
                            w->browser_bookmarks[bi].title, COL_TEXT, COL_TRANSPARENT);
            /* [x] remove button */
            gfx_str(bm_x + bm_w - 20, by + 4, "x", COL_MUTED, COL_TRANSPARENT);
        }
        if (w->browser_bookmark_count == 0)
            gfx_str(bm_x + 8, bm_y + 8, "No bookmarks", COL_MUTED, COL_TRANSPARENT);
    }
```

- [ ] **Step 7: Add bookmark save helper**

```c
static void bookmarks_save(window_t *w) {
    char buf[4096]; u32 pos = 0;
    for (u32 bi = 0; bi < w->browser_bookmark_count; bi++) {
        const char *t = w->browser_bookmarks[bi].title;
        const char *u = w->browser_bookmarks[bi].url;
        u32 tl = (u32)kstrlen(t), ul = (u32)kstrlen(u);
        if (pos + tl + ul + 3 < 4095) {
            kmemcpy(buf+pos,t,tl); pos+=tl;
            buf[pos++]='|';
            kmemcpy(buf+pos,u,ul); pos+=ul;
            buf[pos++]='\n';
        }
    }
    buf[pos]='\0';
    char bm_path[64];
    ksprintf(bm_path, "/home/%s/.bookmarks", user_current_name());
    fs_node_t *f = vfs_resolve_path(bm_path);
    if (!f) {
        char home_path[48]; ksprintf(home_path, "/home/%s", user_current_name());
        fs_node_t *home = vfs_resolve_path(home_path);
        if (home) f = vfs_mkfile(home, ".bookmarks");
    }
    if (f) vfs_write(f, buf, pos);
}
```

- [ ] **Step 8: Update app_browser_click for tabs and bookmarks**

In `app_browser_click()`, at the very top add tab strip hit detection:

```c
    i32 tab_strip_h = 32;
    i32 tab_w = (cr.w - 80) / (i32)(w->browser_tab_count < 1 ? 1 : w->browser_tab_count);
    if (tab_w > 200) tab_w = 200;

    /* Tab clicks */
    if (y >= cr.y && y < cr.y + tab_strip_h) {
        /* New tab button */
        i32 new_tab_x = cr.x + (i32)w->browser_tab_count * (tab_w + 2);
        if (x >= new_tab_x && x < new_tab_x + 32 && w->browser_tab_count < 4) {
            browser_tab_t *nt = &w->browser_tabs[w->browser_tab_count++];
            kmemset(nt, 0, sizeof(browser_tab_t));
            kstrcpy(nt->url, "about:home"); kstrcpy(nt->title, "New Tab");
            nt->url_active = true;
            w->browser_active_tab = w->browser_tab_count - 1;
            return;
        }
        /* Existing tab click / close */
        for (u32 ti = 0; ti < w->browser_tab_count; ti++) {
            i32 tx2 = cr.x + (i32)ti * (tab_w + 2);
            if (x >= tx2 && x < tx2 + tab_w) {
                /* Close button */
                if (x >= tx2 + tab_w - 18 && w->browser_tab_count > 1) {
                    /* Remove tab ti */
                    for (u32 k = ti; k < w->browser_tab_count - 1; k++)
                        w->browser_tabs[k] = w->browser_tabs[k+1];
                    w->browser_tab_count--;
                    if (w->browser_active_tab >= w->browser_tab_count)
                        w->browser_active_tab = w->browser_tab_count - 1;
                } else {
                    w->browser_active_tab = ti;
                }
                return;
            }
        }
        return;
    }
    /* Shift all y comparisons below by tab_strip_h */
    cr.y += tab_strip_h; cr.h -= tab_strip_h;
```

Add Ctrl+D bookmark and ★ button handling in the toolbar area:

```c
    /* ★ bookmarks button (place before Go button) */
    i32 star_x = cr.x + cr.w - go_w - 4 - btn_sz - 6;
    if (y >= btn_y && y < btn_y + btn_sz && x >= star_x && x < star_x + btn_sz) {
        w->browser_bookmarks_open = !w->browser_bookmarks_open;
        return;
    }

    /* Bookmark dropdown interaction */
    if (w->browser_bookmarks_open) {
        i32 bm_x = cr.x + cr.w - 160;
        i32 bm_y = cr.y;
        i32 bm_w = 154;
        i32 row_h = FH + 10;
        if (x >= bm_x && x < bm_x + bm_w) {
            for (u32 bi = 0; bi < w->browser_bookmark_count; bi++) {
                i32 by = bm_y + 4 + (i32)bi * row_h;
                if (y >= by && y < by + row_h) {
                    if (x >= bm_x + bm_w - 20) {
                        /* Remove */
                        for (u32 k = bi; k < w->browser_bookmark_count - 1; k++)
                            w->browser_bookmarks[k] = w->browser_bookmarks[k+1];
                        w->browser_bookmark_count--;
                        bookmarks_save(w);
                    } else {
                        smart_navigate(w, w->browser_bookmarks[bi].url);
                        w->browser_bookmarks_open = false;
                    }
                    return;
                }
            }
            w->browser_bookmarks_open = false;
            return;
        }
        w->browser_bookmarks_open = false;
    }
```

- [ ] **Step 9: Handle Ctrl+D in app_browser_key**

In `app_browser_key()`, add:

```c
    extern bool keyboard_ctrl_held(void);
    if (keyboard_ctrl_held() && c == 4) { /* Ctrl+D */
        if (w->browser_bookmark_count < 32) {
            browser_bookmark_t *bm = &w->browser_bookmarks[w->browser_bookmark_count++];
            kstrncpy(bm->title, ATAB_TITLE(w), 63);
            kstrncpy(bm->url,   ATAB_URL(w),   255);
            bookmarks_save(w);
            notify_push("Browser", "Bookmarked!", COL_GREEN);
        }
        return;
    }
    if (keyboard_ctrl_held() && c == 20) { /* Ctrl+T */
        if (w->browser_tab_count < 4) {
            browser_tab_t *nt = &w->browser_tabs[w->browser_tab_count++];
            kmemset(nt, 0, sizeof(browser_tab_t));
            kstrcpy(nt->url, "about:home"); kstrcpy(nt->title, "New Tab");
            nt->url_active = true;
            w->browser_active_tab = w->browser_tab_count - 1;
        }
        return;
    }
    if (keyboard_ctrl_held() && c == 23) { /* Ctrl+W */
        if (w->browser_tab_count > 1) {
            u32 at = w->browser_active_tab;
            for (u32 k = at; k < w->browser_tab_count - 1; k++)
                w->browser_tabs[k] = w->browser_tabs[k+1];
            w->browser_tab_count--;
            if (w->browser_active_tab >= w->browser_tab_count)
                w->browser_active_tab = w->browser_tab_count - 1;
        }
        return;
    }
```

- [ ] **Step 10: Update gui/wm.c call site — app_browser_click → keep name but add mouse_t param**

In `gui/gui.h`, update the prototype:

```c
void app_browser_click(window_t *w, i32 x, i32 y);   /* keep existing signature */
```

No rename needed — the existing signature is fine, mouse wheel is handled separately in the key handler via scroll keys.

- [ ] **Step 11: Build and verify**

```bash
make clean && make 2>&1 | tail -20
```

- [ ] **Step 12: Commit**

```bash
git add gui/gui.h apps/app_browser.c
git commit -m "feat: browser tabs (4 max), bookmarks with VFS persistence, Ctrl+D/T/W"
```

---

## Task 10: Browser — Find in Page + Mouse Wheel + Chunked HTTP

**Files:**
- Modify: `apps/app_browser.c` (find bar, scroll keys, source view)
- Modify: `net/net.c` (http_decode_chunked)
- Modify: `gui/wm.c` (pass scroll_delta to browser)

- [ ] **Step 1: Add http_decode_chunked to net/net.c**

In `net/net.c`, add after existing HTTP helpers:

```c
/* Decode chunked Transfer-Encoding in-place. buf is the raw body (after headers).
   len = body bytes. out_len receives decoded length. */
void http_decode_chunked(char *buf, u32 len, u32 *out_len) {
    char *src = buf, *dst = buf;
    char *end = buf + len;
    *out_len = 0;
    while (src < end) {
        /* Read chunk size hex line */
        u32 chunk_size = 0;
        while (src < end && (*src == '\r' || *src == '\n')) src++;
        while (src < end && *src != '\r' && *src != '\n') {
            char h = *src++;
            u32 v = 0;
            if (h>='0'&&h<='9') v=h-'0';
            else if (h>='a'&&h<='f') v=h-'a'+10;
            else if (h>='A'&&h<='F') v=h-'A'+10;
            else break;
            chunk_size = chunk_size*16 + v;
        }
        /* Skip CRLF after size */
        while (src < end && (*src == '\r' || *src == '\n')) src++;
        if (chunk_size == 0) break;
        /* Copy chunk data */
        u32 to_copy = (u32)(end - src);
        if (to_copy > chunk_size) to_copy = chunk_size;
        if (dst != src) kmemcpy(dst, src, to_copy);
        dst += to_copy; src += chunk_size;
        *out_len += to_copy;
        /* Skip trailing CRLF */
        while (src < end && (*src == '\r' || *src == '\n')) src++;
    }
    *dst = '\0';
}
```

Add prototype to `include/kernel.h`:

```c
void http_decode_chunked(char *buf, u32 len, u32 *out_len);
```

- [ ] **Step 2: Call http_decode_chunked in app_browser_navigate**

In `app_browser_navigate()`, after extracting the body and before `kmemcpy` to `ATAB_CONT(w)`, add:

```c
    /* Check for chunked transfer encoding */
    if (bs_strstr(g_resp, "Transfer-Encoding: chunked") ||
        bs_strstr(g_resp, "transfer-encoding: chunked")) {
        u32 decoded_len = 0;
        http_decode_chunked((char *)body, (u32)(n - (int)(body - g_resp)), &decoded_len);
        body_len = decoded_len;
    }
```

- [ ] **Step 3: Add find bar to app_browser_draw**

In `app_browser_draw()`, after the status bar and before `gfx_clear_clip()`, add:

```c
    /* Find bar */
    if (w->browser_find_active) {
        i32 fb_h = 28;
        i32 fb_y = cr.y + cr.h - BROW_STAT_H - fb_h;
        gfx_rect(cr.x, fb_y, cr.w, fb_h, COL_SURFACE2);
        gfx_hline(cr.x, fb_y, cr.w, COL_BORDER);
        gfx_str(cr.x + 8, fb_y + (fb_h - FH)/2, "Find:", COL_DIM, COL_TRANSPARENT);
        i32 fi_x = cr.x + 52, fi_w = cr.w - 160;
        gfx_rect_rounded(fi_x, fb_y + 4, fi_w, fb_h - 8, 4, COL_INPUT_BG);
        gfx_rect_rounded_outline(fi_x, fb_y + 4, fi_w, fb_h - 8, 4, COL_PRIMARY);
        gfx_str_clipped(fi_x + 4, fb_y + (fb_h - FH)/2, fi_w - 8,
                        w->browser_find_buf, COL_TEXT, COL_TRANSPARENT);
        char cnt[32]; ksprintf(cnt, "%u/%u", w->browser_find_current + 1, w->browser_find_count);
        gfx_str(fi_x + fi_w + 8, fb_y + (fb_h - FH)/2, cnt, COL_DIM, COL_TRANSPARENT);
    }
```

- [ ] **Step 4: Add find logic to app_browser_key**

In `app_browser_key()`, add:

```c
    if (w->browser_find_active) {
        if (c == 27) { w->browser_find_active = false; return; }
        if (c == '\n') {
            /* Next match */
            if (w->browser_find_count > 0) {
                w->browser_find_current = (w->browser_find_current + 1) % w->browser_find_count;
            }
            return;
        }
        if (c == '\b' && w->browser_find_len > 0)
            w->browser_find_buf[--w->browser_find_len] = '\0';
        else if (c >= 32 && c < 127 && w->browser_find_len < 127) {
            w->browser_find_buf[w->browser_find_len++] = c;
            w->browser_find_buf[w->browser_find_len]   = '\0';
        }
        /* Recount matches in raw content */
        w->browser_find_count   = 0;
        w->browser_find_current = 0;
        u32 pl = (u32)kstrlen(w->browser_find_buf);
        const char *cont = ATAB_CONT(w);
        u32 clen = (u32)kstrlen(cont);
        if (pl > 0) {
            for (u32 i = 0; i + pl <= clen && w->browser_find_count < 64; i++) {
                if (kstrncmp(cont + i, w->browser_find_buf, pl) == 0) {
                    w->browser_find_matches[w->browser_find_count++] = i;
                }
            }
        }
        return;
    }

    extern bool keyboard_ctrl_held(void);
    if (keyboard_ctrl_held() && c == 6) { /* Ctrl+F */
        w->browser_find_active = true;
        w->browser_find_buf[0] = '\0'; w->browser_find_len = 0;
        w->browser_find_count  = 0; w->browser_find_current = 0;
        return;
    }
    if (keyboard_ctrl_held() && c == 21) { /* Ctrl+U source view */
        w->browser_view_source = !w->browser_view_source;
        return;
    }

    /* Scroll shortcuts */
    if (!ATAB(w)->url_active) {
        if (c == ' ' || c == '\n')  ATAB_SCROLL(w) += FH * 20;
        else if (c == '\b')          ATAB_SCROLL(w) -= FH * 20;
        else if (c == 'j')           ATAB_SCROLL(w) += FH * 3;
        else if (c == 'k')           ATAB_SCROLL(w) -= FH * 3;
        if (ATAB_SCROLL(w) < 0) ATAB_SCROLL(w) = 0;
    }
```

- [ ] **Step 5: Source view mode in app_browser_draw**

In the content area section of `app_browser_draw()`, wrap the HTML render in:

```c
    if (w->browser_view_source) {
        /* Render raw content as preformatted text */
        rctx_t ctx = {0};
        ctx.cx = cr.x + 16; ctx.cy = cont_y + 12 - ATAB_SCROLL(w);
        ctx.base_lm = cr.x + 16; ctx.lm = cr.x + 16;
        ctx.rm = cr.x + cr.w - 24; ctx.ct = cont_y; ctx.cb = cont_y + cont_h;
        ctx.fg = COL_DIM; ctx.pre = true;
        render_html(&ctx, ATAB_CONT(w));
    } else {
        /* Normal HTML render */
        /* ... existing render_html call ... */
    }
```

- [ ] **Step 6: Mouse wheel scroll (via gui/wm.c)**

In `gui/wm.c`, in `wm_handle_mouse()`, find where `app_browser_click()` is called. After/around it, add scroll delta handling:

```c
        if (w->app == APP_BROWSER && m->scroll_delta != 0) {
            extern void app_browser_scroll(window_t *w, i32 delta);
            app_browser_scroll(w, m->scroll_delta);
        }
```

Add `app_browser_scroll` to `apps/app_browser.c`:

```c
void app_browser_scroll(window_t *w, i32 delta) {
    ATAB_SCROLL(w) -= delta * FH * 3;
    if (ATAB_SCROLL(w) < 0) ATAB_SCROLL(w) = 0;
}
```

Add prototype to `gui/gui.h`:

```c
void app_browser_scroll(window_t *w, i32 delta);
```

- [ ] **Step 7: Build and verify**

```bash
make clean && make 2>&1 | tail -20
```

- [ ] **Step 8: Commit**

```bash
git add apps/app_browser.c net/net.c include/kernel.h gui/wm.c gui/gui.h
git commit -m "feat: browser find-in-page, mouse wheel, chunked HTTP, source view"
```

---

## Task 11: Browser — HTML Improvements

**Files:**
- Modify: `apps/app_browser.c` (table layout, GET forms, more entities)

- [ ] **Step 1: Extend rctx_t for table support**

In `app_browser.c`, add to the `rctx_t` struct:

```c
    bool in_table;
    i32  col_idx;
    i32  ncols;
    i32  table_col_w;
    char form_action[192];
    bool in_form;
```

- [ ] **Step 2: Add table and form tag handling in render_html**

In the `if (!closing)` block in `render_html()`, add:

```c
                else if (tag_eq(name,"table")) {
                    if (c->cx>c->lm) r_nl(c);
                    c->in_table=true; c->col_idx=0; c->ncols=3;
                    c->table_col_w=(c->rm-c->lm)/c->ncols;
                }
                else if (tag_eq(name,"tr")) {
                    if (c->cx>c->lm) r_nl(c);
                    c->col_idx=0; c->cy+=2; c->cx=c->lm;
                }
                else if (tag_eq(name,"td")||tag_eq(name,"th")) {
                    if (c->col_idx>0) c->cx=c->lm+c->col_idx*c->table_col_w;
                    c->col_idx++;
                    if (tag_eq(name,"th")) c->bold=true;
                }
                else if (tag_eq(name,"form")) {
                    c->in_form=true;
                    /* Extract action attribute */
                    const char *ap=bs_strstr(attrs,"action=");
                    if(ap){
                        ap+=7; char q=0;
                        if(*ap=='"'||*ap=='\'') q=*ap++;
                        u32 ai=0;
                        while(*ap&&ai<191){
                            if(q&&*ap==q) break;
                            if(!q&&(*ap==' '||*ap=='>')) break;
                            c->form_action[ai++]=*ap++;
                        }
                        c->form_action[ai]='\0';
                    }
                }
                else if (tag_eq(name,"input")) {
                    /* Render a visible text input placeholder */
                    i32 iw=120, ih=FH+6;
                    if(c->cx+iw>c->rm) r_nl(c);
                    if(c->cy>=c->ct&&c->cy+ih<=c->cb){
                        gfx_rect_rounded(c->cx,c->cy,iw,ih,4,COL_INPUT_BG);
                        gfx_rect_rounded_outline(c->cx,c->cy,iw,ih,4,COL_BORDER);
                        gfx_str(c->cx+4,c->cy+3,"[input]",COL_MUTED,COL_TRANSPARENT);
                    }
                    c->cx+=iw+4;
                }
                else if (tag_eq(name,"button")||
                         (tag_eq(name,"input")&&bs_strstr(attrs,"submit"))) {
                    i32 bw=60, bh=FH+8;
                    if(c->cx+bw>c->rm) r_nl(c);
                    if(c->cy>=c->ct&&c->cy+bh<=c->cb){
                        gfx_rect_rounded(c->cx,c->cy,bw,bh,4,COL_PRIMARY);
                        gfx_str(c->cx+6,c->cy+4,"Go",COL_WHITE,COL_TRANSPARENT);
                    }
                    c->cx+=bw+4;
                }
```

In the `else` (closing tag) block, add:

```c
                else if (tag_eq(name,"table")) { c->in_table=false; if(c->cx>c->lm) r_nl(c); }
                else if (tag_eq(name,"td")||tag_eq(name,"th")) { if(tag_eq(name,"th")) c->bold=false; }
                else if (tag_eq(name,"form")) { c->in_form=false; }
```

- [ ] **Step 3: Add more HTML entities**

In the entity handling block (the `if (*p == '&')` section), add after the existing entities:

```c
                else if (kstrcmp(ent,"mdash")==0)  ec='-';
                else if (kstrcmp(ent,"ndash")==0)  ec='-';
                else if (kstrcmp(ent,"laquo")==0)  ec='<';
                else if (kstrcmp(ent,"raquo")==0)  ec='>';
                else if (kstrcmp(ent,"hellip")==0) ec='.';
                else if (kstrcmp(ent,"trade")==0)  ec='T';
                else if (kstrcmp(ent,"euro")==0)   ec='E';
                else if (ent[0]=='#' && ent[1]>='0' && ent[1]<='9') {
                    /* Numeric entity &#NNN; */
                    u32 n2=0;
                    for(u32 ei=1;ent[ei]>='0'&&ent[ei]<='9';ei++) n2=n2*10+(ent[ei]-'0');
                    if(n2>=32&&n2<127) ec=(char)n2;
                }
```

- [ ] **Step 4: Build and verify**

```bash
make clean && make 2>&1 | tail -20
```

- [ ] **Step 5: Final boot test**

```bash
make run
```

Verify in QEMU:
- Browser opens, tabs work (Ctrl+T opens new tab, Ctrl+W closes)
- Navigate to `http://example.com`, check HTML renders
- Ctrl+F opens find bar
- Settings > Display tab shows resolution list
- File manager: select file, press R to rename
- Editor: Ctrl+F opens find bar, Ctrl+O opens file dialog

- [ ] **Step 6: Final commit**

```bash
git add apps/app_browser.c
git commit -m "feat: browser table layout, GET form rendering, extended HTML entities"
```

---

## Summary of All Files Changed

| File | Change |
|------|--------|
| `kernel/pipe.c` | NEW — pipe buffer pool |
| `drivers/vesa.c` | NEW — BGA mode switching |
| `include/kernel.h` | pipe_t, signal API, vesa API, chunked decode, kb_push_char |
| `kernel/scheduler.c` | pending_signals in tcb_t, signal_send, signal delivery tick |
| `drivers/keyboard.c` | kb_push → kb_push_char (exposed) |
| `shell/shell.c` | Ctrl+C abort, pipe operator, grep built-in, kill built-in |
| `kernel/settings.c` | vesa_w/h fields, settings_set_vesa_mode |
| `kernel/kernel.c` | vesa_init call, /usr/bin /var/pkg creation |
| `kernel/carepkg.c` | install/remove with VFS writes, dep resolution, DB |
| `apps/app_settings.c` | Display tab |
| `apps/app_files.c` | copy/paste, rename, drag initiation, preview pane |
| `apps/app_editor.c` | find/replace bar, open/save dialog |
| `apps/app_pkgmgr.c` | Installed tab, detail pane, progress dots |
| `apps/app_browser.c` | Tabs, bookmarks, find, wheel, chunked, HTML tables/forms |
| `apps/app_browser.c` | Source view, scroll shortcuts |
| `net/net.c` | http_decode_chunked |
| `gui/gui.h` | browser_tab_t, bookmark_t, clip_mode_t, drag_file_t, window_t fields |
| `gui/wm.c` | drag ghost, browser scroll delta, drag_file_t/g_clip globals |
| `Makefile` | Add pipe.c, vesa.c |
