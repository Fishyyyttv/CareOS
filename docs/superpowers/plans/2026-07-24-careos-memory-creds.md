# CareOS Memory Footprint & Credential Hygiene — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Cut kernel `.bss` from 833.5 MiB to ~193.5 MiB by moving VFS file data to the heap, and ensure no weak password shipped in source survives first boot.

**Architecture:** Three independent components. C rewrites a text file. A changes `fs_node_t.data` from a 5 MiB inline array to a heap pointer and converts 35 call sites. B adds a forced-password-change flag and replaces a 32-bit password hash with PBKDF2-HMAC-SHA256, migrating the on-disk userdb to v4.

**Tech Stack:** Freestanding C (`-ffreestanding -nostdlib`, x86-64), custom `kmalloc`/`kfree`, existing `net/sha256.c` for HMAC, WSL Ubuntu 24.04 toolchain, QEMU for boot verification.

**Spec:** `docs/superpowers/specs/2026-07-24-careos-memory-creds-design.md`

## Global Constraints

- All `git` commands MUST use native Windows git, never WSL git. WSL git writes `/mnt/d/...` paths into worktree metadata that Windows git reports as `prunable`.
- All builds run through WSL: `wsl -e bash -c 'cd /mnt/d/Users/mhetm/Downloads/CareOS_v9_full/CareOS_v9/.claude/worktrees/mem-creds && make ...'`. There is no native Windows toolchain.
- No standard library. Use `kmalloc`, `kfree`, `kmemset`, `kmemcpy`, `kstrcpy`, `kstrncpy`, `kstrlen`, `kstrcmp`, `serial_write`. There is no `krealloc`.
- The kernel is compiled `-mno-sse -mno-mmx -mno-sse2 -mno-red-zone`. Do not introduce floating point.
- `DOOM1.WAD` is gitignored and absent from the worktree. Symlink it from the parent checkout before building.
- Boot tests override `DISK_MB` to avoid writing a 4 GB image; `D:` has only 32 GB free.
- There is no unit test framework in this repo. "Test" means: compile, inspect symbol sizes with `nm`/`size`, and boot in QEMU checking the serial log. Verification is empirical, not assertion-based.
- Baseline to beat: `.bss` = 874,037,824 B; `node_pool` = `0x2800b400`.

---

## Task 0: Build the unmodified tree to establish a working baseline

Do not skip. If the tree does not build before any edits, a later build failure is ambiguous.

**Files:** none modified.

- [ ] **Step 1: Link the gitignored WAD into the worktree**

```bash
cd "D:/Users/mhetm/Downloads/CareOS_v9_full/CareOS_v9/.claude/worktrees/mem-creds"
cp ../../../DOOM1.WAD DOOM1.WAD
```

- [ ] **Step 2: Build**

```bash
wsl -e bash -c 'cd /mnt/d/Users/mhetm/Downloads/CareOS_v9_full/CareOS_v9/.claude/worktrees/mem-creds && make kernel/kernel.elf 2>&1 | tail -20'
```

Expected: links successfully, produces `kernel/kernel.elf`.

- [ ] **Step 3: Record the baseline numbers**

```bash
wsl -e bash -c 'cd /mnt/d/.../mem-creds && size -A kernel/kernel.elf | grep -E "bss|Total"; nm -S --size-sort kernel/vfs.o | tail -2'
```

Expected: `.bss 874037824`, `node_pool ... 000000002800b400`.

If these differ from the spec's baseline, stop and reconcile before continuing.

---

## Task 1: Component C — rewrite `BUILD_INFO.txt` for v9

Independent of all other tasks. Lands first because it cannot break the build.

**Files:**
- Modify: `BUILD_INFO.txt` (entire contents)

**Interfaces:** none.

- [ ] **Step 1: Gather the real v7–v9 history**

```bash
cd "D:/Users/mhetm/Downloads/CareOS_v9_full/CareOS_v9/.claude/worktrees/mem-creds"
git log --oneline | head -40
ls docs/superpowers/plans/
cat CHANGES_v6.md
```

Use only what these show. Do not invent highlights.

- [ ] **Step 2: Write the file**

Replace the whole file. Keep the v6 block as history rather than deleting it. Structure:

```
CareOS v9 -- Desktop Release Build
Build: July 2026

Highlights:
  [1] Per-process address spaces (ELF user binaries, isolated page tables)
  [2] Cooperative scheduler fixes (task_yield PIC EOI, timer survival)
  [3] rc.care startup scripts and the Care scripting language
  [4] Theming engine with per-user theme preference, desktop pet
  [5] Beefier browser app, TLS 1.3 stack (X25519, AES-GCM, SHA-256)
  [6] carepkg package manager, ext2 read/write support

Previously, in v6:
  [1] Proper GUI login gate (no auto-login)
  [2] Login lockout after repeated failures
  [3] Refined staged splash/loader screen
  [4] Fixed launcher overlay rendering + input handling
  [5] VFS hardening: duplicate-name prevention and recursive delete cleanup
  [6] Safer VFS path generation with bounded writes
```

Adjust the v9 highlight list to match what `git log` and the plan filenames actually show.

- [ ] **Step 3: Commit**

```bash
git add BUILD_INFO.txt
git commit -m "Update BUILD_INFO.txt from v6 to v9"
```

---

## Task 2: Component A step 1 — change the struct and add the VFS data API

This task will leave the tree **not compiling**. That is expected and is the point: the type change makes the compiler enumerate every site Task 3 must fix. Do not commit a broken build — Tasks 2 and 3 land as one commit at the end of Task 3.

**Files:**
- Modify: `include/kernel.h:222-253`
- Modify: `kernel/vfs.c` (add the three new functions near the top, after `alloc_node`)

**Interfaces:**
- Produces: `int vfs_file_reserve(fs_node_t *f, u32 bytes)` → 0 on success, -1 on failure.
- Produces: `const char *vfs_file_str(fs_node_t *f)` → never NULL; `""` when the file is empty.
- Produces: `void vfs_file_release(fs_node_t *f)` → frees `data` only when `data_owned`.
- Produces: `#define FS_FILE_MAX_BYTES (16u*1024u*1024u)`
- Removes: `FS_FILE_DATA_MAX`, `fs_node_t.raw_data`.

- [ ] **Step 1: Edit the struct in `include/kernel.h`**

Replace lines 222-242:

```c
#define FS_MAX_FILES    64
#define FS_MAX_DIRS     64
#define FS_NAME_MAX     64
#define FS_PATH_MAX     256

/* Upper bound on a single file's heap allocation. The kernel heap is 192 MiB
 * (KERNEL_HEAP_SIZE); this ceiling stops one runaway write from consuming it. */
#define FS_FILE_MAX_BYTES (16u*1024u*1024u)

typedef enum { FS_FILE, FS_DIR } fs_node_type_t;

typedef struct fs_node {
    char            name[FS_NAME_MAX];
    fs_node_type_t  type;
    u32             size;        /* valid bytes in data */
    char           *data;        /* heap or borrowed; NULL until content exists */
    u32             capacity;    /* allocated bytes; 0 when borrowed */
    bool            data_owned;  /* false = borrowed memory, never kfree'd */
    struct fs_node *parent;
    struct fs_node *children[32];
    u32             child_count;
    u32             permissions;   /* rwxrwxrwx bitmask */
    u32             inode_num;     /* ext2 inode number; 0 = in-memory only */
    bool            children_loaded;
} fs_node_t;
```

- [ ] **Step 2: Declare the new API in `include/kernel.h`**

Add after the existing `vfs_resolve_path` declaration:

```c
int         vfs_file_reserve(fs_node_t *f, u32 bytes);
const char *vfs_file_str(fs_node_t *f);
void        vfs_file_release(fs_node_t *f);
```

- [ ] **Step 3: Implement the three functions in `kernel/vfs.c`**

Insert after `alloc_node` (around line 50):

```c
/* -- File data storage ------------------------------------------------------
 * File contents live on the kernel heap, allocated on first write. A node with
 * data_owned == false borrows memory it must never free (e.g. the DOOM WAD
 * embedded in .data). */

void vfs_file_release(fs_node_t *f) {
    if (!f) return;
    if (f->data && f->data_owned) kfree(f->data);
    f->data = NULL;
    f->capacity = 0;
    f->data_owned = false;
    f->size = 0;
}

int vfs_file_reserve(fs_node_t *f, u32 bytes) {
    if (!f) return -1;
    if (bytes > FS_FILE_MAX_BYTES) return -1;

    /* +1 so callers can always NUL-terminate at [size]. */
    u32 need = bytes + 1u;
    if (f->data && f->data_owned && f->capacity >= need) return 0;

    u32 cap = f->capacity ? f->capacity : 64u;
    while (cap < need) {
        if (cap > FS_FILE_MAX_BYTES / 2u) { cap = need; break; }
        cap *= 2u;
    }

    char *buf = (char *)kmalloc(cap);
    if (!buf) return -1;
    kmemset(buf, 0, cap);

    /* Copy forward whatever content already existed, borrowed or owned. */
    if (f->data && f->size) {
        u32 keep = f->size < bytes ? f->size : bytes;
        kmemcpy(buf, f->data, keep);
    }
    if (f->data && f->data_owned) kfree(f->data);

    f->data = buf;
    f->capacity = cap;
    f->data_owned = true;
    return 0;
}

const char *vfs_file_str(fs_node_t *f) {
    return (f && f->data) ? f->data : "";
}
```

- [ ] **Step 4: Confirm the compiler now lists the work**

```bash
wsl -e bash -c 'cd /mnt/d/.../mem-creds && make kernel/kernel.elf 2>&1 | grep -E "error|warning" | head -40'
```

Expected: errors in `vfs.c`, `syscall.c`, `elf.c`, `carepkg.c`, `kernel.c`, `libc_shim.c`, `shell.c`, and the three apps — specifically about `raw_data` no longer existing and `FS_FILE_DATA_MAX` being undeclared. Cross-check the file list against the 10-file table in the spec. Do NOT commit.

---

## Task 3: Component A step 2 — convert all 35 call sites

Every site must be audited for the new NULL case: `f->data` used to be a valid buffer always, and is now `NULL` for an empty file. A missed site is a kernel NULL dereference, not a graceful error.

**Rule of thumb:** read-only sites become `vfs_file_str(f)`. Write sites call `vfs_file_reserve(f, n)` first and handle failure. Sites casting to `const u8 *` for binary data keep using `f->data` but must gain a NULL guard.

**Files (all modified):** `kernel/vfs.c` (10 sites), `shell/shell.c` (5), `kernel/elf.c` (4), `kernel/syscall.c` (3), `kernel/carepkg.c` (3), `apps/app_files.c` (3), `apps/app_terminal.c` (3), `apps/app_editor.c` (2), `kernel/kernel.c` (1), `kernel/libc_shim.c` (1)

**Interfaces:**
- Consumes: `vfs_file_reserve`, `vfs_file_str`, `vfs_file_release`, `FS_FILE_MAX_BYTES` from Task 2.

- [ ] **Step 1: `kernel/vfs.c` — `vfs_ext2_cache_file` (was lines 65-77)**

```c
static void vfs_ext2_cache_file(fs_node_t *node) {
    ext2_inode_t ino;
    if (!node || node->type != FS_FILE || node->inode_num == 0) return;
    if (ext2_read_inode(node->inode_num, &ino) != 0) return;

    u32 to_read = ino.i_size;
    if (to_read > FS_FILE_MAX_BYTES) to_read = FS_FILE_MAX_BYTES;
    if (vfs_file_reserve(node, to_read) != 0) return;

    if (to_read > 0) {
        if (ext2_read_data(&ino, 0, node->data, to_read) < 0) return;
    }
    node->data[to_read] = '\0';
    node->size = to_read;
}
```

Note the behaviour fix: `size` is now set to what was actually read, not to `ino.i_size`, so `size` can never exceed the buffer.

- [ ] **Step 2: `kernel/vfs.c` — `vfs_write` (was lines 497-517)**

```c
int vfs_write(fs_node_t *f, const char *data, u32 len) {
    if (!f || f->type != FS_FILE || !data) return -1;
    if (len > FS_FILE_MAX_BYTES) len = FS_FILE_MAX_BYTES;

    if (f->inode_num != 0) {
        int written = (int)vfs_ext2_write(f, 0, len, (const u8*)data);
        if (written > 0) {
            u32 cached = (u32)written;
            if (cached > FS_FILE_MAX_BYTES) cached = FS_FILE_MAX_BYTES;
            if (vfs_file_reserve(f, cached) != 0) return -1;
            kmemcpy(f->data, data, cached);
            f->data[cached] = '\0';
            f->size = cached;
        }
        return written;
    }

    if (vfs_file_reserve(f, len) != 0) return -1;
    kmemcpy(f->data, data, len);
    f->data[len] = '\0';
    f->size = len;
    homefs_maybe_save(f);
    return (int)len;
}
```

- [ ] **Step 3: `kernel/vfs.c` — `vfs_read` (was lines 519-527), collapse the dual path**

```c
int vfs_read(fs_node_t *f, char *buf, u32 len) {
    if (!f || f->type != FS_FILE || !buf) return -1;
    if (f->inode_num != 0) {
        int n = (int)vfs_ext2_read(f, 0, len, (u8*)buf);
        if (n >= 0) vfs_ext2_cache_file(f);
        return n;
    }
    if (!f->data) return 0;
    u32 n = f->size < len ? f->size : len;
    kmemcpy(buf, f->data, n);
    return (int)n;
}
```

- [ ] **Step 4: `kernel/vfs.c` — free heap data in `vfs_wipe_subtree` (was lines 528-537)**

This is the leak-prevention site. `kmemset` alone would drop the pointer.

```c
static void vfs_wipe_subtree(fs_node_t *node) {
    if (!node) return;
    while (node->child_count > 0) {
        fs_node_t *child = node->children[node->child_count - 1];
        node->child_count--;
        vfs_wipe_subtree(child);
    }
    vfs_file_release(node);
    kmemset(node, 0, sizeof(fs_node_t));
}
```

- [ ] **Step 5: `kernel/vfs.c` — `vfs_copy` (was lines 679-688)**

```c
int vfs_copy(fs_node_t *src, fs_node_t *dst_dir, const char *new_name) {
    if (!src || src->type != FS_FILE || !dst_dir) return -1;
    fs_node_t *n = vfs_mkfile(dst_dir, new_name ? new_name : src->name);
    if (!n) return -1;
    if (src->size == 0 || !src->data) { n->size = 0; return 0; }
    if (vfs_file_reserve(n, src->size) != 0) return -1;
    kmemcpy(n->data, src->data, src->size);
    n->size = src->size;
    n->data[n->size] = '\0';
    homefs_maybe_save(dst_dir);
    return 0;
}
```

- [ ] **Step 6: `kernel/vfs.c` — fix the `u16` truncation in `homefs_serialize_node` (was line ~232)**

The old code clamped against `FS_FILE_DATA_MAX` (5 MiB) then truncated into a `u16`, silently corrupting any file over 64 KiB. Bound it against the field's real limit and say so.

```c
    if (rel[0] != '\0') {
        u32 plen = (u32)kstrlen(rel);
        u32 dlen = (node->type == FS_FILE && node->data) ? node->size : 0;

        /* data_len is u16 on disk: anything larger cannot be persisted. */
        if (dlen > 0xFFFFu) {
            serial_write("[homefs] file too large to persist, skipping data: ");
            serial_write(rel);
            serial_write("\n");
            dlen = 0;
        }
        ...
```

Keep the rest of the function as-is, including the `homefs_append(node->data, dlen, ...)` call, which is now guarded by `dlen == 0` when `data` is NULL.

- [ ] **Step 7: `kernel/vfs.c` — remaining sites**

Line ~103 (`vfs_ext2_cache_child`) needs no change. The seed-data calls near line 660-676 all go through `vfs_write`, so they need no change. Search for any remaining `FS_FILE_DATA_MAX` and `raw_data` in the file and remove them:

```bash
wsl -e bash -c 'cd /mnt/d/.../mem-creds && grep -n "FS_FILE_DATA_MAX\|raw_data" kernel/vfs.c'
```

Expected: no output.

- [ ] **Step 8: `kernel/kernel.c:103` and the WAD borrow at :277**

The ELF injection at line 103 must reserve first:

```c
    if (vfs_file_reserve(n, elf_size) != 0) return;
    kmemcpy(n->data, start, elf_size);
    n->size = elf_size;
```

The WAD becomes a borrow instead of using the removed `raw_data`:

```c
                    if (wad_node) {
                        wad_node->data       = (char *)_binary_DOOM1_WAD_start;
                        wad_node->capacity   = 0;
                        wad_node->data_owned = false;   /* .data, never kfree */
                        wad_node->size       = wad_size;
                        wad_node->inode_num  = 0;
```

- [ ] **Step 9: `kernel/libc_shim.c:115-117` — collapse the dual path**

```c
    const char *src = fh->node->data ? fh->node->data : "";
```

- [ ] **Step 10: `kernel/syscall.c:90, 109-111`**

The read at :90 needs a NULL guard:

```c
    if (!node->data) return 0;
    kmemcpy(buf, node->data + fd_table[fd].offset, n);
```

The append at :109-111 must reserve for the grown size:

```c
    if (vfs_file_reserve(node, node->size + n) != 0) return -1;
    kmemcpy(node->data + node->size, buf, n);
    node->size += n;
    node->data[node->size] = '\0';
```

Check the surrounding code for how `node->size` was previously advanced and do not double-count it.

- [ ] **Step 11: `kernel/elf.c:75, 131, 226, 231`**

All four are read-only casts. Add a NULL guard at each function's entry and keep `node->data`:

```c
    if (!node || !node->data) return -1;   /* or the function's existing error value */
    const u8 *data = (const u8 *)node->data;
```

At :226 and :231, match the existing return convention (`elf_validate(...)` returns an int; the `:231` site returns a pointer, so guard with `return NULL`).

- [ ] **Step 12: `kernel/carepkg.c:112, 219, 378`**

All read-only. Use the accessor:

```c
    const char *buf = vfs_file_str(node);          /* :112 */
    if (mf) vfs_write(mf, vfs_file_str(node), node->size);   /* :219 */
    const char *p = vfs_file_str(db);              /* :378 */
```

- [ ] **Step 13: `shell/shell.c:202-203, 264, 286, 701, 751`**

```c
    terminal_write(vfs_file_str(f));                                   /* :202 */
    if(f->size && vfs_file_str(f)[f->size-1] != '\n') terminal_write("\n");
    const char *p = vfs_file_str(f); char line2[256];                  /* :264 */
    char c = vfs_file_str(f)[i]; if(c=='\n') lines++;                  /* :286 */
    if (care_lang_exec(vfs_file_str(f), f->size) != 0) ...             /* :701 */
    if (motd && motd->data) { terminal_write(motd->data); ... }        /* :751 unchanged */
```

`:751` already NULL-guards `data` and works correctly against a pointer.

- [ ] **Step 14: `apps/app_terminal.c:91-92, 260`**

```c
        win_append(w, vfs_file_str(f));                                     /* :91 */
        if(f->size && vfs_file_str(f)[f->size-1] != '\n') win_append(w,"\n");
        if(care_lang_exec_buf(vfs_file_str(f), f->size, outbuf, sizeof(outbuf)) != 0)  /* :260 */
```

- [ ] **Step 15: `apps/app_editor.c:419, 561` and `apps/app_files.c:209, 429, 456`**

All are `kmemcpy` into a fixed window buffer, or `win_append`. Guard the copies:

```c
    if (f->data && load_len) kmemcpy(w->text_buf, f->data, load_len);      /* editor :419 */
    if (ch->data && load_len) kmemcpy(w->text_buf, ch->data, load_len);    /* editor :561 */
    if (sel_node->data && plen) kmemcpy(preview, sel_node->data, plen);    /* files :209 */
    if(ch->size>0) win_append(ew, vfs_file_str(ch));                       /* files :429 */
    if(child->size>0) win_append(ew, vfs_file_str(child));                 /* files :456 */
```

- [ ] **Step 16: Build clean**

```bash
wsl -e bash -c 'cd /mnt/d/.../mem-creds && make kernel/kernel.elf 2>&1 | tail -20'
```

Expected: no errors, no new warnings. Then confirm nothing was missed:

```bash
wsl -e bash -c 'cd /mnt/d/.../mem-creds && grep -rn "FS_FILE_DATA_MAX\|raw_data" --include="*.c" --include="*.h" kernel/ apps/ gui/ shell/ net/ drivers/ include/'
```

Expected: no output.

- [ ] **Step 17: Verify the memory win — this is the primary objective evidence**

```bash
wsl -e bash -c 'cd /mnt/d/.../mem-creds && size -A kernel/kernel.elf | grep -E "bss|Total"; nm -S --size-sort kernel/vfs.o | tail -2'
```

Expected: `.bss` ≈ 202,930,000 B (~193.5 MiB), down from 874,037,824. `node_pool` a few tens of KB, down from `0x2800b400`. If `.bss` did not move, the struct change did not take effect — stop and investigate.

- [ ] **Step 18: Boot test**

```bash
wsl -e bash -c 'cd /mnt/d/.../mem-creds && make run QEMU_DISPLAY=none DISK_MB=64 2>&1 | tail -40'
```

Inspect the serial log for VFS init and `[users] subsystem ready`. Confirm no page fault or panic. If `make run` insists on the display, invoke QEMU directly with `-nographic -serial mon:stdio`.

- [ ] **Step 19: Commit Tasks 2 and 3 together**

```bash
git add include/kernel.h kernel/vfs.c kernel/kernel.c kernel/libc_shim.c kernel/syscall.c kernel/elf.c kernel/carepkg.c shell/shell.c apps/app_terminal.c apps/app_editor.c apps/app_files.c
git commit -m "Move VFS file data to the heap, cutting .bss from 833 MiB to 194 MiB"
```

---

## Task 4: Component B step 1 — fix the userdb save overflow and grow the region

Two pre-existing bugs found during design, both of which block the larger v4 record. Land them before the format change so the fix is reviewable on its own.

**Files:**
- Modify: `include/kernel.h:466` (`CAREOS_DISK_USERDB_SECTORS`)
- Modify: `Makefile:45` (`DISK_RESERVED_SECTORS`)
- Modify: `kernel/users.c:369-405` (`users_persist_save`)

**Interfaces:** none new.

- [ ] **Step 1: Understand the bug before changing it**

`users_persist_load` bound-checks (`payload_len > max_payload` → bail), but `users_persist_save` does not. It writes `user_count × sizeof(userdb_entry_v3_t)` (156 B) plus a 16 B header into `userdb_io`, which is only `4 × 512 = 2048` B. At 14 users that is 2200 B — a 152 B overflow past a static buffer, corrupting whatever follows it in BSS. `MAX_USERS` is 16, and `useradd` can reach it.

- [ ] **Step 2: Grow the region in `include/kernel.h:466`**

```c
#define CAREOS_DISK_USERDB_SECTORS     8u
```

8 sectors = 4096 B, which holds 16 v4 records (198 B each = 3168 B) plus the header with headroom.

- [ ] **Step 3: Keep the Makefile in sync — `Makefile:45`**

`CAREOS_DISK_RESERVED_SECTORS` is `96 + 4 + 8 = 108`. The Makefile hardcodes the same number and must match or the ext2 filesystem will overlap the reserved tail.

```makefile
DISK_RESERVED_SECTORS := 108
```

- [ ] **Step 4: Add the missing bound check in `users_persist_save`**

Insert after `hdr->count = user_count;`:

```c
    u32 max_payload = USERDB_SECTORS * USERDB_SECTOR_SIZE - (u32)sizeof(userdb_hdr_t);
    u32 max_entries = max_payload / (u32)sizeof(userdb_entry_v4_t);
    u32 to_write = user_count < max_entries ? user_count : max_entries;
    if (to_write < user_count) {
        serial_write("[users] WARNING: userdb region too small, truncating save\n");
    }
    hdr->count = to_write;
```

Then bound the write loop with `to_write` instead of `user_count`. (Use `userdb_entry_v3_t` here if Task 5 has not landed yet; the entry type name changes in Task 5.)

- [ ] **Step 5: Build and boot**

```bash
wsl -e bash -c 'cd /mnt/d/.../mem-creds && make kernel/kernel.elf 2>&1 | tail -10'
```

Expected: clean. Boot as in Task 3 Step 18 and confirm `[users]` still initialises. Because the region moved, an existing on-disk userdb now fails its magic check and the defaults are re-seeded — that is expected and safe.

- [ ] **Step 6: Commit**

```bash
git add include/kernel.h Makefile kernel/users.c
git commit -m "Fix unbounded userdb save overflowing its static buffer at 14+ users"
```

---

## Task 5: Component B step 2 — PBKDF2-HMAC-SHA256 and forced password change

**Files:**
- Modify: `kernel/users.c` (record struct, hashing, v4 format, migration, seeding)
- Modify: `include/kernel.h` (declare `user_must_change_password`, `hmac_sha256` if not already visible)
- Modify: `gui/gui.c:377-410` (`login_try` and the login state machine)

**Interfaces:**
- Consumes: `void hmac_sha256(const u8 *key, u32 klen, const u8 *msg, u32 mlen, u8 *out)` from `net/sha256.c`.
- Produces: `bool user_must_change_password(void)` — true when the logged-in user must change their password before proceeding.

- [ ] **Step 1: Confirm `hmac_sha256` is declared where `users.c` can see it**

```bash
wsl -e bash -c 'cd /mnt/d/.../mem-creds && grep -rn "hmac_sha256\|void sha256" include/'
```

If it is only declared in a net-local header, add the prototype to `include/kernel.h`.

- [ ] **Step 2: Add the v4 record and widen the in-memory record**

In `kernel/users.c`, bump the version and add the new entry type beside the existing v1/v2/v3 structs:

```c
#define USERDB_VERSION 4u

#define HASH_LEGACY_FNV 0u   /* 32-bit hash from v1-v3, upgrade on next login */
#define HASH_PBKDF2_S256 1u

typedef struct __attribute__((packed)) {
    u32  uid;
    u32  gid;
    u8   hash_algo;
    u8   must_change;
    u8   pass_hash[32];
    u8   salt[16];
    u8   active;
    u8   is_root;
    char name[32];
    char home[64];
    char shell[32];
    u32  theme_pref;
    u16  last_login_year;
    u8   last_login_month;
    u8   last_login_day;
    u8   last_login_hour;
    u8   last_login_minute;
} userdb_entry_v4_t;
```

In `user_rec_t`, replace `u32 pass_hash` and `u32 salt` with:

```c
    u8   hash_algo;
    u8   pass_hash[32];
    u8   salt[16];
    bool must_change_password;
```

- [ ] **Step 3: Implement the KDF**

```c
/* PBKDF2-HMAC-SHA256, one output block (dkLen = 32). */
static void pbkdf2_sha256(const char *pw, const u8 *salt, u32 slen,
                          u32 iters, u8 *out32) {
    u8 block[64];
    u32 pwlen = (u32)kstrlen(pw);

    /* U1 = HMAC(pw, salt || INT32BE(1)) */
    if (slen > 48u) slen = 48u;
    kmemcpy(block, salt, slen);
    block[slen+0] = 0; block[slen+1] = 0; block[slen+2] = 0; block[slen+3] = 1;

    u8 u[32], t[32];
    hmac_sha256((const u8*)pw, pwlen, block, slen + 4u, u);
    kmemcpy(t, u, 32);

    for (u32 i = 1; i < iters; i++) {
        hmac_sha256((const u8*)pw, pwlen, u, 32, u);
        for (u32 j = 0; j < 32; j++) t[j] ^= u[j];
    }
    kmemcpy(out32, t, 32);
}

#define PBKDF2_ITERS 4096u
```

- [ ] **Step 4: Generate a real salt**

```c
static u64 rdtsc_now(void) {
    u32 lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return ((u64)hi << 32) | lo;
}

/* Not a CSPRNG. Adequate for a hobby kernel; do not reuse for key material. */
static void user_make_salt(u8 *salt16) {
    rtc_time_t t; rtc_read(&t);
    u64 a = rdtsc_now();
    u64 b = ((u64)t.year << 40) ^ ((u64)t.month << 32) ^ ((u64)t.day << 24)
          ^ ((u64)t.hour << 16) ^ ((u64)t.minute << 8) ^ (u64)t.second
          ^ ((u64)timer_get_ticks() << 3);
    for (u32 i = 0; i < 8; i++)  salt16[i]     = (u8)(a >> (i * 8));
    for (u32 i = 0; i < 8; i++)  salt16[8 + i] = (u8)(b >> (i * 8));
}
```

Check `rtc_time_t` actually has a `second` field before using it; drop that term if not.

- [ ] **Step 5: Rewrite verification and setting**

```c
static void user_set_password(user_rec_t *u, const char *pw) {
    user_make_salt(u->salt);
    pbkdf2_sha256(pw, u->salt, 16u, PBKDF2_ITERS, u->pass_hash);
    u->hash_algo = HASH_PBKDF2_S256;
}

static bool user_verify_password(user_rec_t *u, const char *pw) {
    if (u->hash_algo == HASH_PBKDF2_S256) {
        u8 got[32];
        pbkdf2_sha256(pw, u->salt, 16u, PBKDF2_ITERS, got);
        u8 diff = 0;
        for (u32 i = 0; i < 32; i++) diff |= (u8)(got[i] ^ u->pass_hash[i]);
        return diff == 0;   /* constant time over the digest */
    }

    /* Legacy v1-v3: verify with the old 32-bit algorithm, then upgrade. */
    u32 legacy_salt = 0;
    kmemcpy(&legacy_salt, u->salt, 4);
    u32 want = 0;
    kmemcpy(&want, u->pass_hash, 4);
    if (hash_password_salted(pw, legacy_salt) != want) return false;

    user_set_password(u, pw);   /* transparent rehash */
    users_persist_save();
    serial_write("[users] upgraded legacy password hash to PBKDF2\n");
    return true;
}
```

Keep the old `hash_password_salted` and `simple_hash` functions — they are still needed to verify legacy records. Update `user_login`, `user_change_password`, `user_passwd`, and `user_add_internal` to call `user_set_password` / `user_verify_password` instead of touching `pass_hash` directly.

- [ ] **Step 6: Migrate v1/v2/v3 loaders to the new in-memory shape**

In each of the three existing legacy branches of `users_persist_load`, replace `u->pass_hash = entries[i].pass_hash;` and `u->salt = ...;` with:

```c
            kmemset(u->pass_hash, 0, sizeof(u->pass_hash));
            kmemcpy(u->pass_hash, &entries[i].pass_hash, 4);
            kmemset(u->salt, 0, sizeof(u->salt));
            kmemcpy(u->salt, &entries[i].salt, 4);      /* omit for v1: no salt field */
            u->hash_algo = HASH_LEGACY_FNV;
            u->must_change_password = false;
```

Add a fourth branch for `hdr->version == 4u` that reads `userdb_entry_v4_t` directly, copying `hash_algo`, `pass_hash`, `salt`, and `must_change`.

- [ ] **Step 7: Seed with the forced-change flag**

In `users_init`, both seeding sites (was lines 706-715):

```c
        user_add(0, 0, "root", "root", "/root", true);
        user_add(1000, 1000, "user", "CareOS123", "/home/user", false);
        for (u32 i = 0; i < user_count; i++) users[i].must_change_password = true;
        users_persist_save();
```

The bootstrap passwords stay, but cannot survive first boot. Add a comment saying exactly that, so the next reader does not "fix" it by inventing a stronger literal.

- [ ] **Step 8: Expose the flag**

```c
bool user_must_change_password(void) {
    user_rec_t *u = find_user_by_uid(current_uid);
    return u ? u->must_change_password : false;
}
```

Declare it in `include/kernel.h`. Clear it in `user_change_password` on success.

- [ ] **Step 9: Gate the GUI login**

In `gui/gui.c`, `login_try` currently does this on success (line ~395):

```c
    if (user_login(s->username, s->password) == 0) {
        login_set_status(s, "Login successful. Launching desktop...", COL_GREEN);
```

Add a branch: if `user_must_change_password()` is true, switch the login state machine into a change-password mode instead of proceeding to the desktop. `login_mode_t` (line 216) already exists as an enum — add a `LOGIN_MODE_MUST_CHANGE` member and handle it in `draw_login_screen` with two fields (new password, confirm) and a status line. On submit, call `user_change_password`; on success clear the flag and proceed, on failure show why (`password_is_strong` requires ≥8 chars with upper, lower, and digit).

- [ ] **Step 10: Build**

```bash
wsl -e bash -c 'cd /mnt/d/.../mem-creds && make kernel/kernel.elf 2>&1 | tail -20'
```

Expected: clean.

- [ ] **Step 11: Boot test — fresh userdb**

Boot with a fresh disk image. Expected: login as `root`/`root` is accepted, then the desktop is NOT reached; the change-password screen appears. Setting `NewPass123` is accepted; setting `short` is rejected. After the change, `[users]` persists and a reboot logs in with the new password only.

- [ ] **Step 12: Measure the KDF cost**

Watch for a perceptible stall at login. 4096 iterations is 8192 HMAC-SHA256 invocations in software. If login stalls noticeably, lower `PBKDF2_ITERS` and record the measured figure in a comment. Do not raise it without measuring.

- [ ] **Step 13: Commit**

```bash
git add include/kernel.h kernel/users.c gui/gui.c
git commit -m "Replace 32-bit password hash with PBKDF2-HMAC-SHA256, force change of seeded passwords"
```

---

## Task 6: Final verification and summary

- [ ] **Step 1: Full clean build**

```bash
wsl -e bash -c 'cd /mnt/d/.../mem-creds && make clean && make 2>&1 | tail -20'
```

- [ ] **Step 2: Final numbers against the spec's acceptance criteria**

```bash
wsl -e bash -c 'cd /mnt/d/.../mem-creds && size -A kernel/kernel.elf | grep -E "bss|Total"; nm -S --size-sort kernel/vfs.o | tail -2'
```

- [ ] **Step 3: File I/O smoke test in the booted OS**

In the GUI terminal: `touch a`, `echo hi`, `cat a`, `rm a`, and `care` on a `.cl` script. Confirm `DOOM1.WAD` still loads (launch the DOOM app) — that exercises the borrowed-data path.

- [ ] **Step 4: Report**

State the before/after `.bss` figures, what was verified by boot versus only by compile, and anything left undone.

---

## Self-Review

**Spec coverage.** Component A → Tasks 2, 3. Component B → Tasks 4, 5. Component C → Task 1. Verification section → Tasks 0, 3 (steps 17-18), 6. Two spec items are covered by tasks added beyond the original three components: the `u16` homefs truncation (Task 3 Step 6) and the userdb save overflow (Task 4), both discovered during design. The spec's deferred findings (#1, #2, #3) correctly have no tasks.

**Placeholder scan.** No TBD/TODO. Three steps intentionally direct verification rather than prescribing an edit: Task 1 Step 1 (read real history before writing release notes — inventing them is the failure mode being avoided), Task 5 Step 1 (`hmac_sha256` declaration location), and Task 5 Step 4 (`rtc_time_t.second` existence). Each names the exact command and the decision it settles.

**Type consistency.** `vfs_file_reserve`/`vfs_file_str`/`vfs_file_release` keep identical signatures between Task 2 and their uses in Task 3. `FS_FILE_MAX_BYTES` is used consistently. `userdb_entry_v4_t` is introduced in Task 5 Step 2 but referenced in Task 4 Step 4 — Task 4 Step 4 flags this ordering explicitly and says to use `userdb_entry_v3_t` until Task 5 lands. `user_must_change_password()` matches between Task 5 Steps 8 and 9.

**Known ordering hazard.** Task 4 changes the disk layout, so it invalidates any existing on-disk userdb, settings, and homefs state. This is called out in Task 4 Step 5 and is safe because all three regions are magic/checksum validated and fall back to defaults.
