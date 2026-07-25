# CareOS v9 — Memory Footprint & Credential Hygiene

**Date:** 2026-07-24
**Branch:** `feat/mem-creds-hygiene` (worktree off `master` @ `d1a167a`)
**Status:** Approved, ready for implementation planning

## Context

An audit of CareOS v9 produced six findings. This spec covers three of them. The
other three are deliberately deferred, each to its own spec.

### In scope

| # | Finding | Verified evidence |
|---|---------|-------------------|
| 5 | ~640 MB static BSS for the VFS node pool | `nm -S kernel/vfs.o`: `node_pool` = `0x2800b400` = 671,132,672 B = **640.0 MiB** |
| 6 | Weak default credentials shipped in source | `kernel/users.c:706-715` seeds `root/root` and `user/CareOS123` |
| 4 | `BUILD_INFO.txt` is stale | Reads "CareOS v6 — March 2026"; tree is v9 |

### Out of scope (deferred)

| # | Finding | Why deferred |
|---|---------|--------------|
| 1 | Session model is single-global, not multi-session | `kernel/users.c:95` — one `static session_t session`. A real multi-session model wants `fork()`/`exec()`/process groups first; `CAREOS_ROADMAP.md` lists all three as not-yet-added. Building it now would be speculative. |
| 2 | `gui/widget.c` under-adopted | Only `apps/app_calc.c` uses it (plus `wm.c:245` creating an empty root panel). The framework is 100 lines with no input handling, hit-testing, or scrolling, and `widget_draw_recursive` ignores its `target` argument. The real decision is *invest or delete*, which deserves its own discussion. |
| 3 | shell/terminal duplication | ~55 commands in `shell/shell.c` vs ~30 in `apps/app_terminal.c`, already diverged. Unifying requires an output-sink abstraction because shell writes via `terminal_write*` (VGA) and the app via `win_append`. |

## Goals

1. Cut kernel `.bss` from **833 MiB** to **~193 MiB** and remove the arbitrary
   per-file size cap.
2. Ensure no weak password shipped in source survives first boot, and store
   password hashes with a real KDF.
3. Make `BUILD_INFO.txt` accurately describe v9.

## Non-goals

- Reducing the 192 MiB static `heap_mem[]` (`kernel/memory.c:11`). After
  Component A it dominates `.bss`, but converting it to frame-backed allocation
  touches early-boot ordering and carries the highest regression risk. Called
  out here as the obvious follow-up.
- Any change to the session model, widget framework, or shell duplication.

## Baseline measurements

From the pre-change build (`size -A kernel/kernel.elf`):

```
.text          445,678
.rodata        139,424
.data       14,541,180   (embedded DOOM1.WAD)
.bss       874,037,824   (833.5 MiB)
```

`.bss` decomposes as `node_pool` (640 MiB) + `heap_mem` (192 MiB) + ~1.5 MiB of
everything else. Any fix to the node pool therefore lands at ~193 MiB, because
the heap then dominates. This was the decisive input to Component A's scoping.

---

## Component A — Heap-backed VFS file data

### Current shape

```c
typedef struct fs_node {
    char            name[FS_NAME_MAX];
    fs_node_type_t  type;
    u32             size;
    char            data[FS_FILE_DATA_MAX];   /* 5 MiB, inline */
    void           *raw_data;                 /* if set, content lives here */
    struct fs_node *parent;
    struct fs_node *children[32];
    u32             child_count;
    u32             permissions;
    u32             inode_num;
    bool            children_loaded;
} fs_node_t;
```

`static fs_node_t node_pool[FS_MAX_FILES + FS_MAX_DIRS]` (`vfs.c:8`) is 128
nodes × ~5.24 MB = 640 MiB of BSS, whether or not any file has content.

`raw_data` already exists as an out-of-line escape hatch, used only for the
embedded DOOM WAD (`kernel/kernel.c:277`). `vfs_read` (`vfs.c:525`) and
`libc_shim.c:115-117` both branch on it.

### Target shape

```c
typedef struct fs_node {
    char            name[FS_NAME_MAX];
    fs_node_type_t  type;
    u32             size;        /* valid bytes in data */
    char           *data;        /* NULL until content exists */
    u32             capacity;    /* allocated bytes; 0 when borrowed */
    bool            data_owned;  /* false = borrowed, never kfree'd */
    struct fs_node *parent;
    struct fs_node *children[32];
    u32             child_count;
    u32             permissions;
    u32             inode_num;
    bool            children_loaded;
} fs_node_t;
```

`raw_data` is removed. The DOOM WAD becomes `data = _binary_DOOM1_WAD_start`,
`data_owned = false`, which collapses the dual paths in `vfs.c:525` and
`libc_shim.c:115-117` into one.

`node_pool` becomes 128 × ~212 B ≈ **27 KB**.

### API

`FS_FILE_DATA_MAX` is deleted. It is replaced by `FS_FILE_MAX_BYTES`
(16 MiB) — a sanity ceiling so a runaway write cannot exhaust the 192 MiB heap.

```c
int         vfs_file_reserve(fs_node_t *f, u32 bytes);  /* ensure capacity */
const char *vfs_file_str(fs_node_t *f);                 /* never NULL; "" if empty */
void        vfs_file_release(fs_node_t *f);             /* kfree if data_owned */
```

- `vfs_file_reserve` grows by doubling, starting at the requested size. Because
  `kernel/memory.c` provides only `kmalloc`/`kfree` and no `krealloc`, growth is
  alloc-copy-free internally. A standalone `krealloc` will be added only if a
  second caller appears.
- `vfs_file_str` exists so read sites have one NULL-safe idiom.
- `vfs_file_release` is called from `vfs_delete` and `vfs_wipe_subtree`.

### Call-site conversion

35 `fs_node->data` sites across 10 files. (`net/net.c` and
`drivers/storage/ata.c` also match `->data` but are unrelated — TCP header
offsets and the ATA sector cache respectively.)

| File | Sites | Nature |
|------|-------|--------|
| `kernel/vfs.c` | 10 | write / read / copy / ext2 cache / persist — the core work |
| `shell/shell.c` | 5 | `cat`, `grep`, `wc`, `care`, motd (`:751` already NULL-guards `data`) |
| `kernel/elf.c` | 4 | read-only casts to `const u8 *` |
| `kernel/syscall.c` | 3 | read + append; append path needs `vfs_file_reserve` |
| `kernel/carepkg.c` | 3 | read-only |
| `apps/app_files.c` | 3 | preview + open |
| `apps/app_terminal.c` | 3 | `cat`, `care` |
| `apps/app_editor.c` | 2 | load-into-buffer |
| `kernel/kernel.c` | 1 | test-ELF injection |
| `kernel/libc_shim.c` | 1 | dual path collapses to one |

### Field naming decision

The field keeps the name `data` while changing type from `char[]` to `char *`.

The alternative considered was renaming it, so the compiler would enumerate
every site. That was rejected because every one of the 35 sites needs a manual
NULL-safety audit regardless, so renaming adds churn without adding safety, and
the table above already serves as the exhaustive checklist. No site uses
`sizeof(node->data)`, which was the specific silent-breakage risk that would
have justified a rename.

### Latent bug folded in

`homefs_serialize_node` (`vfs.c:216-247`) clamps `dlen` against
`FS_FILE_DATA_MAX` and then assigns it to a **`u16`** field
(`eh.data_len = (u16)dlen`). Any file over 64 KiB is therefore already silently
truncated on persist today. This is pre-existing, not caused by this work, but
Component A deletes the constant that clamp reads. Rather than leave a nonsense
clamp behind, A will bound the value correctly against the `u16` field and emit
a serial warning when a file is too large to persist.

### On-disk format

**No migration needed.** The homefs format is
`{u8 type, u16 path_len, u16 data_len}` followed by path and data bytes — it is
path/length based, not a struct dump of `fs_node_t`. Changing the in-memory
layout does not affect it. This was verified before design sign-off and
materially reduced A's assessed risk.

---

## Component B — Credential hygiene

Two independent fixes.

### B1 — Force password change on first login

Add `bool must_change_password` to `user_rec_t`, set on both seeded accounts.
`login_try` (`gui/gui.c:377`) routes into a mandatory change-password screen
when the flag is set; the new password is validated by the existing
`password_is_strong()`, then the flag is cleared and the DB persisted.

`root/root` remains a documented bootstrap credential but cannot survive first
boot.

This also fixes a policy inconsistency: `password_is_strong()`
(`users.c:129`) requires ≥8 characters with upper, lower, and digit — and
`"root"` fails all four. The seed path reaches `user_add` directly, skipping the
check that `user_create_common` enforces, so CareOS currently ships a password
it would reject if typed.

### B2 — Real KDF

`hash_password_salted` (`users.c:113`) runs 512 mixing rounds but returns a
**`u32`**. A 32-bit output is trivially brute-forced offline, and any colliding
string authenticates — the rounds buy far less than the width costs.

`net/sha256.c` already provides `sha256`, `hmac_sha256`, and HKDF, and is
already linked into the kernel. So:

- **PBKDF2-HMAC-SHA256**, 4096 iterations (tunable; the figure will be
  confirmed against measured boot cost during implementation).
- 128-bit salt, 256-bit stored hash.
- Salt entropy from mixing `rdtsc`, RTC, and tick count. This is adequate here
  but is not a CSPRNG, and the code will say so in a comment. Current salt
  derivation leans on `timer_get_ticks()` (`users.c:107`), which is near-constant
  at first boot.

### Migration

The userdb already carries three versioned on-disk record formats in
`users_persist_load`, so a format bump is an established pattern.

A hash cannot be upgraded without the plaintext password. Therefore v4 records
carry a `hash_algo` tag:

1. Records migrated from v1–v3 keep their legacy 32-bit hash and are tagged
   `HASH_LEGACY`.
2. Login verifies a `HASH_LEGACY` record against the old algorithm.
3. On the next **successful** login, the record is transparently rehashed with
   PBKDF2 and retagged.

This is the standard password-hash upgrade pattern and avoids forcing a reset on
existing installs.

`USERDB_SECTORS` may need to grow for the larger record; this will be verified
against the actual record count rather than assumed.

---

## Component C — `BUILD_INFO.txt`

Rewrite for v9, sourced from the tree rather than invented: `git log` since the
v6 content, `CHANGES_v6.md`, and the v7–v9 plan documents under
`docs/superpowers/plans/`. Existing v6 highlights are retained as history rather
than deleted.

---

## Implementation order

1. **C** — trivial, independent, no build risk.
2. **A** — largest and riskiest; must pass a QEMU boot before proceeding.
3. **B** — depends on nothing in A.

A and B touch disjoint files except `include/kernel.h`.

## Verification

Each component must pass before the next begins.

- **Build:** via WSL Ubuntu 24.04 (`gcc`, `ld`, `nasm`, `qemu-system-x86_64`,
  `grub-mkrescue` all present). No native Windows toolchain exists.
- **Component A sizing:** `size -A kernel/kernel.elf` must show `.bss` drop from
  874,037,824 B (833.5 MiB) to ≈202,930,000 B (~193.5 MiB), and `nm -S kernel/vfs.o`
  must show `node_pool` fall from `0x2800b400` to a few tens of KB. This is the
  primary objective evidence. All figures in this spec are MiB (1024-based) unless
  a raw byte count is given.
- **Boot:** QEMU headless; serial log must show VFS init, users subsystem ready,
  and a successful login.
- **Component B behaviour:** first boot with a fresh userdb must refuse to reach
  the desktop until the seeded password is changed; a userdb from the previous
  format must still authenticate and then silently upgrade to PBKDF2.
- **File I/O sanity:** write, read, `cat`, and delete a file; confirm a file
  larger than the old 5 MiB cap now works; confirm `DOOM1.WAD` still loads via
  the borrowed-data path.

### Build environment notes

- `DISK_MB` defaults to 4096, and `D:` has only 32 GB free at 97% full. Boot
  tests will override it with a smaller image rather than write a 4 GB file.
- The worktree does not contain `DOOM1.WAD` (gitignored). It will be linked or
  copied from the parent checkout at build time.
- All `git` invocations must use native Windows git, **not** WSL git. A WSL-created
  worktree writes `/mnt/d/...` paths into `.git`, which Windows git cannot
  resolve and reports as `prunable`. This was hit once already and cleaned up.
- The worktree was created from local `HEAD`, not the harness default. Local
  `master` is 25 commits ahead of `origin/main`, and the default `worktree.baseRef`
  of `fresh` would have branched from `origin/main`, silently dropping the
  per-process address-space work this change builds on.

## Risks

| Risk | Severity | Mitigation |
|------|----------|------------|
| Missed NULL check on `f->data` after A | High — kernel NULL deref, not a graceful error | Exhaustive 35-site checklist; `vfs_file_str()` as the single safe read idiom |
| Heap exhaustion now possible where BSS previously guaranteed space | Medium | `FS_FILE_MAX_BYTES` ceiling; `vfs_file_reserve` returns failure and callers surface it |
| Use-after-free / double-free on `data` | Medium | `data_owned` flag; single release path via `vfs_file_release` |
| Locking out an existing install via B | Medium | Legacy hashes verified against the old algorithm, never invalidated; upgrade only after successful login |
| Concurrent edits from the other two agents | Low | Isolated worktree on its own branch |
