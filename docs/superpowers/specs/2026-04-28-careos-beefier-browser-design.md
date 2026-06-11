# CareOS v9 — Beefier OS + Browser Design

**Date:** 2026-04-28  
**Approach:** Foundation-first (kernel → graphics → apps → browser)

---

## Overview

This spec covers six independent but ordered layers of improvement to CareOS v9:

1. Kernel: Pipes & Signals (B1, B2)
2. Graphics: VESA mode switching & EDID (D3)
3. File Manager: copy/paste, rename, drag-and-drop, previews (C2)
4. Code Editor: line numbers, syntax highlighting, search/replace, open/save dialog (C3)
5. Package Manager: real install/remove, dependency tracking, persistent DB (C4)
6. Browser: tabs, bookmarks, find-in-page, mouse-wheel, chunked HTTP, better HTML (browser)

Each layer is self-contained; later layers benefit from earlier ones (e.g. the editor Ctrl+C uses signals, the settings Display tab uses VESA).

---

## Section 1: Kernel — Pipes & Signals

### Pipes

**Data structure:** `pipe_t` — a 4096-byte circular buffer with `read_pos`, `write_pos`, `len`, `closed` flag. Lives in `kernel/pipe.c`.

**API (added to `kernel.h`):**
```c
pipe_t *pipe_create(void);
int     pipe_write(pipe_t *p, const u8 *buf, u32 len);
int     pipe_read(pipe_t *p, u8 *buf, u32 maxlen);
void    pipe_close(pipe_t *p);
```

**Shell integration:** `shell.c` pipe parser detects `|` in the command line, splits into left/right segments. Left command runs with stdout redirected to `pipe_write()`; right command runs with stdin sourced from `pipe_read()`. Since shell commands are synchronous kernel-mode functions, this is **buffered sequential piping** — left writes all output to the pipe buffer, then right reads from it. Sufficient for `ls | grep foo`, `cat file | wc`, etc. Max pipe buffer 4096 bytes; output truncated with a warning if exceeded.

**Shell built-ins enabled by pipes:** `|` operator, pipe chaining up to 4 stages.

**Files changed:**
- `kernel/pipe.c` — new
- `include/kernel.h` — pipe_t struct, pipe API
- `shell/shell.c` — pipe parser, redirected stdout/stdin hooks

### Signals

**Data added to `task_t`:**
```c
u32          pending_signals;          /* bitmask */
task_func_t  signal_handlers[32];      /* per-signal handler */
bool         task_killed;
```

**Signal numbers:** `SIGINT=2`, `SIGKILL=9` (only these two for now).

**Delivery:** `signal_send(u32 task_id, u32 sig)` sets the bit in `task->pending_signals`. The scheduler checks `pending_signals` on every tick before resuming a task; if set, it calls the handler or calls `task_exit()` for `SIGKILL`.

**Ctrl+C routing:** The keyboard driver detects `Ctrl+C` scancode → calls `signal_send(focused_task_id, SIGINT)`. The shell task catches `SIGINT` itself (registered handler) to abort the current command line and redraw the prompt instead of dying.

**Shell built-in:** `kill <pid>` calls `signal_send(pid, SIGKILL)`.

**Files changed:**
- `include/kernel.h` — updated `task_t`, `signal_send()` prototype
- `kernel/scheduler.c` — signal delivery in scheduler tick
- `drivers/keyboard.c` — Ctrl+C → SIGINT routing
- `shell/shell.c` — SIGINT handler registration, `kill` built-in

---

## Section 2: Graphics — VESA Mode Switching

### Real-mode Trampoline

A 64-byte 16-bit stub is assembled into `drivers/vesa_trampoline.S` and linked to load at physical address `0x7E00`. The trampoline:
1. Switches from long mode → protected mode → real mode
2. Executes a VBE INT 10h call (mode number + function in a shared struct at `0x7E80`)
3. Returns to long mode and signals completion via a flag at `0x7E84`

`vesa_call(u16 ax, u16 bx, void *buf)` in `drivers/vesa.c` fills the shared struct and jumps to the trampoline.

### VBE Mode Enumeration

`vesa_init()` called from `kernel_main()` after paging:
1. `INT 10h AX=4F00h` → fills `VbeInfoBlock` at `0x7F00` (max 512 bytes), reads `VideoModePtr` list
2. For each mode ID: `INT 10h AX=4F01h` → `ModeInfoBlock`; keep modes where `bpp==32` and `w>=800`
3. Stores up to 16 valid `vesa_mode_t {u16 id; u32 w,h; u32 fb_addr; u32 pitch;}` in a static array

### EDID Query

`vesa_get_edid(u8 *buf128)` calls `INT 10h AX=4F15h BL=01h`. Parses preferred timing descriptor (bytes 38–45) for native width/height. Stored in `g_edid_native_w`, `g_edid_native_h`. Used to mark the preferred mode in the Settings UI.

### Runtime Mode Switch

`vesa_set_mode(u16 mode_id)`:
1. Calls trampoline with `INT 10h AX=4F02h BX=mode_id|0x4000` (linear framebuffer bit)
2. On success: reads new `fb_addr` and `pitch` from mode info, calls `gfx_init(fb_addr, w, h, pitch)`
3. On failure: shows error notification, keeps current mode
4. Persists `mode_id` to `careos_settings_t.vesa_mode`; `vesa_init()` re-applies it on boot

### Settings UI

New "Display" tab in `app_settings.c`:
- Lists available VBE modes as clickable rows (e.g. `1920×1080 32bpp`)
- Native resolution flagged with `(Native)` label
- "Apply" button calls `vesa_set_mode()`
- Current resolution shown at top

**Files changed:**
- `drivers/vesa.c` — new (trampoline caller, enumeration, EDID, set_mode)
- `drivers/vesa_trampoline.S` — new (16-bit stub)
- `include/kernel.h` — vesa_mode_t, vesa API
- `kernel/kernel.c` — call `vesa_init()` after paging
- `kernel/settings.c` + `careos_settings_t` — add `vesa_mode` field
- `apps/app_settings.c` — Display tab

---

## Section 3: File Manager Improvements

### Copy/Paste

Extend existing clipboard (`g_clipboard`, `g_clipboard_len` in `gui.h`) with:
```c
typedef enum { CLIP_NONE, CLIP_FILE_COPY, CLIP_FILE_CUT } clip_mode_t;
extern clip_mode_t g_clip_mode;
extern char        g_clip_path[FS_PATH_MAX];
```

- **Ctrl+C** on selected file: `g_clip_mode = CLIP_FILE_COPY`, copy path
- **Ctrl+X**: `g_clip_mode = CLIP_FILE_CUT`
- **Ctrl+V** in any Files window: call `vfs_copy()` (copy) or `vfs_rename()` (cut/move); show success notification; clear clip_mode if cut

### Inline Rename

New `window_t` fields: `bool fm_renaming`, `textinput_t fm_rename_input`.

- **F2** or double-click on selected file name → `fm_renaming = true`, `fm_rename_input` pre-filled with current name
- Input drawn over the file name cell in the list
- **Enter** → `vfs_rename(node, fm_rename_input.buf)`, `fm_renaming = false`
- **Escape** → cancel

### Drag-and-Drop

Global in `gui/wm.c`:
```c
typedef struct { bool active; char path[FS_PATH_MAX]; window_t *src; } drag_file_t;
extern drag_file_t g_drag_file;
```

- Drag starts when left button held + mouse moved >5px on a selected file in a Files window
- `wm_handle_mouse()` draws a ghost label (file name) at cursor position during drag
- On release: if drop target is a different Files window, call `vfs_copy()` into target directory; show notification
- Same-window drop: no-op for now

### File Previews

Right panel (right 1/3 of Files window when width > 500px) shows:
- **Text files** (`.txt`, `.c`, `.h`, `.sh`, `.md`): first 8 lines via `gfx_str_clipped`
- **Directories**: child count + `[Directory]` label
- **Binaries**: file size + `[Binary]` label
- **No file selected**: empty panel with placeholder text

**Files changed:**
- `apps/app_files.c` — all file manager features
- `gui/gui.h` + `window_t` — `fm_renaming`, `fm_rename_input`
- `gui/wm.c` — global drag state, ghost render
- `include/kernel.h` — `clip_mode_t`, `g_clip_mode`, `g_clip_path`

---

## Section 4: Code Editor Improvements

### Line Numbers

Fixed 48px left gutter rendered before text content. Content area shifts right 48px. Each visible line draws its number in `COL_DIM`. The cursor's current line draws a full-width highlight strip in `COL_SURFACE2`.

### Syntax Highlighting

`highlight_state_t` tracks: `in_string` (char delimiter), `in_block_comment`, `in_line_comment`. Language detected from `editor_path` extension at file open:
- `.c`, `.h` → C mode: keywords, `#` preprocessor, `//` and `/* */` comments, `"..."` strings
- `.sh` → shell mode: `#` comments, `$VAR` variables
- other → plain (no highlight)

Rendering walks visible lines character by character, switching `gfx_char()` color based on state machine output. Keyword list stored as a static `const char *[]` array, matched with a simple prefix scan.

### Search / Replace

New `window_t` fields: `char editor_find_buf[128]`, `char editor_replace_buf[128]`, `bool editor_find_active`, `bool editor_replace_active`, `u32 editor_match_pos`, `u32 editor_match_count`.

- **Ctrl+F**: `editor_find_active = true` → 28px find bar at bottom; typing updates `editor_match_pos` to first match; matched substrings highlighted with `COL_SELECTION` background during draw
- **Enter / Shift+Enter**: cycle to next/previous match, scroll view
- **Ctrl+H**: also shows replace bar; Enter replaces current match, Ctrl+Enter replaces all occurrences
- **Escape**: close bars

### Open / Save Dialog

`bool editor_dialog_open`, `bool editor_dialog_save`, `fs_node_t *editor_dialog_dir`, `u32 editor_dialog_sel`, `textinput_t editor_dialog_name_input` added to `window_t`.

Dialog drawn as a full-window overlay rect in `app_editor_draw()`:
- Directory listing (arrow keys / click to navigate)
- Filename input at bottom
- Enter confirms: **open** reads file into editor buffer; **save** calls `vfs_write()`
- Escape cancels

**Files changed:**
- `apps/app_editor.c` — all editor features
- `gui/gui.h` + `window_t` — find/replace/dialog fields

---

## Section 5: Package Manager Improvements

### Real Install / Remove

`carepkg_install(const char *pkg_name)`:
1. Ensure `/usr/bin` and `/usr/lib` exist in VFS (create if absent)
2. For each file in package's `files[]` list: `vfs_mkfile()` + `vfs_write()` with static content defined inline in `carepkg.c` (shell scripts, config files, small text utilities — no binary blobs)
3. Append entry to `/var/pkg/installed.db`: `name|version|file1,file2,...\n`
4. Flush DB to ATA disk

`carepkg_remove(const char *pkg_name)`:
1. Read `/var/pkg/installed.db`, find entry
2. `vfs_delete()` each listed file
3. Remove entry from DB, flush

### Dependency Tracking

Package metadata struct gains `char deps[128]` (comma-separated names). Before installing, `carepkg_install()` parses deps, checks each against installed DB, recursively installs missing deps. Circular dep detection via a `char visited[8][32]` stack. Max depth 8.

### Persistent Installed Database

`/var/pkg/installed.db` is a plain-text VFS file flushed to disk on every install/remove via the existing ext2 write path. `carepkg_init()` reads it at boot and marks catalog entries as installed.

### UI Improvements

- Three tabs: **Available**, **Installed**, **All**
- Per-package detail pane: description, version, deps, installed file list
- Tick-based progress indicator during install (animated dots in status line)
- Inline error messages for VFS failures or missing deps

**Files changed:**
- `kernel/carepkg.c` — install/remove/deps, DB persistence
- `apps/app_pkgmgr.c` — tabs, detail pane, progress
- `kernel/kernel.c` — create `/usr/bin`, `/var/pkg` at boot
- `include/kernel.h` — updated `carepkg_*` signatures

---

## Section 6: Browser Improvements

### Tabs

New struct:
```c
typedef struct {
    char url[256];
    char content[16384];
    char title[128];
    i32  scroll;
    char history[10][256];
    u32  history_pos, history_count;
} browser_tab_t;
```

`window_t` gains `browser_tab_t browser_tabs[8]`, `u32 browser_tab_count`, `u32 browser_active_tab`. The current flat browser fields become `browser_tabs[0]` at init.

Tab strip: 32px bar above the URL bar. Each tab renders title (clipped), active tab highlighted, × close button. Keyboard: Ctrl+T (new tab), Ctrl+W (close), Ctrl+Tab (cycle).

### Bookmarks

```c
typedef struct { char title[64]; char url[256]; } bookmark_t;
```

Static `browser_bookmarks[32]` array, persisted to `/home/<user>/.bookmarks` (one `title|url` per line) in VFS. Loaded at `app_browser_init()`.

- **Ctrl+D**: bookmark current page (prompt for title using a small inline input)
- **★ button** in toolbar: dropdown list of bookmarks; each row has a click-to-navigate area and a small `[x]` remove button on the right
- Saves to VFS on every add/remove

### Find in Page

New `window_t` fields: `char browser_find_buf[128]`, `bool browser_find_active`, `u32 browser_find_matches[64]`, `u32 browser_find_count`, `u32 browser_find_current`.

- **Ctrl+F**: 28px find bar at bottom; search runs against raw `browser_content` (byte offsets stored in `browser_find_matches`)
- During `render_html()`, when the render position corresponds to a match offset, draw a `COL_SELECTION` background behind those characters
- Enter/Shift+Enter cycles matches, adjusting `browser_scroll` to keep match visible
- Escape closes

### Mouse-Wheel Scroll & Keyboard Shortcuts

`app_browser_mouse(window_t *w, mouse_t *m)` replaces `app_browser_click()`:
- `m->scroll_delta != 0` → `browser_scroll += scroll_delta * FH * 3`
- Arrow Up/Down → `±FH*3`, Page Up/Down → `±FH*20`, Home → 0, End → max scroll

### Chunked Transfer Decoding

`http_decode_chunked(char *buf, u32 len, u32 *out_len)` in `net/net.c`:
- Detects `Transfer-Encoding: chunked` in response headers
- Strips chunk-size hex lines and concatenates chunk bodies in-place
- Called in `app_browser_navigate()` after header/body split when header present

### HTML Improvements

- **Tables**: `rctx_t` gains `in_table`, `col_idx`, `ncols` (detected from first `<tr>` scan). Each `<td>` advances `cx` by `(rm-lm)/ncols`.
- **GET forms**: `<input type=text>` renders a visible placeholder box; `<form method=get action=...>` stores action URL; clicking the input pre-fills the URL bar with `action?field=value`
- **More entities**: `&mdash;` → `—`, `&laquo;` → `<`, `&raquo;` → `>`, `&hellip;` → `...`, numeric `&#NNN;` → ASCII if printable
- **Source view**: Ctrl+U toggles `browser_view_source`; when true, renders `browser_content` raw as monospace text (using `rctx.pre = true`) instead of parsing HTML

**Files changed:**
- `apps/app_browser.c` — tabs, bookmarks, find, mouse, HTML, source view
- `gui/gui.h` + `window_t` — tab array, bookmark array, find fields; `app_browser_mouse()` prototype replaces `app_browser_click()`
- `gui/wm.c` — update call site from `app_browser_click()` to `app_browser_mouse()`
- `net/net.c` — `http_decode_chunked()`

---

## Implementation Order

| Step | Layer | Key files |
|------|-------|-----------|
| 1 | Pipes | `kernel/pipe.c`, `shell/shell.c` |
| 2 | Signals | `kernel/scheduler.c`, `drivers/keyboard.c`, `shell/shell.c` |
| 3 | VESA trampoline + enumeration | `drivers/vesa.c`, `drivers/vesa_trampoline.S` |
| 4 | VESA settings UI | `apps/app_settings.c`, `kernel/settings.c` |
| 5 | File manager improvements | `apps/app_files.c`, `gui/wm.c` |
| 6 | Editor improvements | `apps/app_editor.c` |
| 7 | Package manager improvements | `kernel/carepkg.c`, `apps/app_pkgmgr.c` |
| 8 | Browser: tabs + bookmarks | `apps/app_browser.c` |
| 9 | Browser: find + scroll + chunked | `apps/app_browser.c`, `net/net.c` |
| 10 | Browser: HTML improvements | `apps/app_browser.c` |

---

## Constraints & Non-Goals

- No JavaScript engine
- No image decoding (browser shows `[img]` placeholder)
- No POST form submission
- No USB support (separate future effort)
- No audio beyond existing PC speaker
- Shell pipes are buffered sequential, not true concurrent fork+exec
- VESA trampoline requires BIOS present (won't work under pure UEFI without CSM)
