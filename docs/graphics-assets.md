# CareOS graphics asset system

Real images for a kernel that only had rectangles. Icons, wallpapers, and the
plumbing that gets them from a build machine into a framebuffer.

Nothing here depends on a library. The decoders are three files totalling under
600 lines, the cache is one flat array, and the artwork is linked into the
kernel image the same way `DOOM1.WAD` already was.

---

## 1. The shape of it

```
  tools/gen-icons.py          offline: SVG theme  ->  assets/careos-icons.cra
  Makefile / objcopy          .cra  ->  .rodata in kernel.elf
  gui/resource_boot.c         mounts the archive at /system
  gui/resource_cache.c        path -> image_t, decoded once
  gui/icon.c                  name -> path, with size and format fallback
  gui/image*.c                bytes -> pixels, pixels -> framebuffer
  gui/launcher.c              draws whatever appdb says the icon is
```

Two rules explain most of the design:

**The pixel is `0xAARRGGBB`, everywhere.** The framebuffer backbuffer already
holds `0x00RRGGBB`; an image is the same thing with an alpha byte. No palettes,
no colour conversion at draw time, no per-format pixel accessors. Every decoder
normalises into that one layout and then the blitter is the only code that
knows about alpha.

**Decode once, at a real size.** Icons are baked at 16/24/32/48/64 by the
offline tool, and the cache keys on `path` and on `path@WxH`. The launcher
redraws sixteen tiles every frame; without this that is a thousand decodes a
second to draw a picture that never changes.

---

## 2. Formats

### `.cri` — CareOS Raster Image (native)

16-byte header, then `width * height * 4` bytes in **B, G, R, A** order —
which, read back as a `u32` on little-endian x86, *is* `0xAARRGGBB`.

That is the entire point of the format. `image_decode_cri()` can hand back an
`image_t` whose `pixels` points **directly into the archive**, with no
allocation and no decode pass. The 145-icon theme costs zero bytes of heap.

| off | size | field |
|----:|-----:|-------|
| 0   | 4    | `"CRI1"` |
| 4   | 2    | width |
| 6   | 2    | height |
| 8   | 1    | encoding (0 = BGRA32, 1 = RLE) |
| 9   | 1    | flags (bit 0: contains non-opaque pixels) |
| 10  | 2    | reserved, 0 |
| 12  | 4    | payload length |
| 16  | …    | payload |

The header is 16 bytes so the payload stays 4-byte aligned; the archive aligns
every entry for the same reason. Misaligned payloads are copied instead of
borrowed rather than risking an unaligned `u32` load.

Encoding 1 is a byte-oriented RLE: a control byte, then either one repeated
BGRA quad (`ctl & 0x80`, `(ctl & 0x7F) + 1` times) or that many literal quads.
Flat icon art compresses by roughly half; the decoder is twenty lines.

### `.bmp`

24/32-bpp uncompressed `BI_RGB` and 32-bpp `BI_BITFIELDS`, bottom-up and
top-down. `BITMAPINFOHEADER` and the V4/V5 extensions. This is what art tools
export, so it is the format to hand someone who asks "how do I add an icon".

Not supported: RLE4/RLE8, palettes, 16-bpp, OS/2 headers. Each is another table
and another loop for a format no icon will arrive in.

### `.tga`

Types 2 (raw) and 10 (RLE) at 24/32 bpp. The smallest decoder of the three.
TGA has no magic number, so it is tried **last** and validates strictly — the
colour-map spec must be all zero, reserved descriptor bits must be clear.
Otherwise a truncated file of any other type "decodes" into noise.

### `.cra` — CareOS Resource Archive

Many `.cri` in one blob so the theme links with a single `objcopy`. A 16-byte
header, a table of 60-byte entries (48-byte name, offset, length, w, h), then
4-byte-aligned payloads. Names are relative paths: `icons/48/browser.cri`,
`wallpapers/default.cri`.

### A note on 32-bit alpha

Plenty of tools write 32-bpp BMP and TGA with the fourth byte left at zero.
Honouring that literally renders the image **completely invisible**, which is
the most common way an image loader appears to "silently do nothing".
`image_classify_alpha()` treats an all-zero alpha channel as *no* alpha channel
and forces it opaque. It also detects the all-opaque case, which lets the
blitter take a straight-copy path forever after.

---

## 3. Why the archive is mounted, not unpacked

The first version of `res_mount_archive()` published each entry as a VFS file
with borrowed data — exactly the `DOOM1.WAD` trick — so
`/system/icons/48/browser.cri` was a real, listable, `cat`-able file.

It was wrong, and the boot log said so: **41 of 145 entries mounted**.

The VFS is one fixed pool of `FS_MAX_FILES + FS_MAX_DIRS` = **128 nodes for the
entire OS** (`kernel/vfs.c`). By `gui_init()` roughly 40 were free. Unpacking
the theme did not merely truncate itself — it consumed every remaining node,
leaving nothing for package installs, user files, or anything else that wanted
one later. A quiet, global failure caused by a feature that looked like it
worked.

So an archive is a **lookup source**. `res_image()` checks the VFS first, then
the mounted archives:

```c
static image_t *load_resource(const char *path) {
    fs_node_t *node = vfs_resolve_path(path);
    if (node && node->type == FS_FILE && node->size > 0) return image_load(path);

    const u8 *data; u32 len;
    if (res_archive_find(path, &data, &len)) return image_load_mem(data, len, false);
    return NULL;
}
```

Zero nodes, and the VFS-first order means a file dropped at the same path still
**overrides** the baked asset. The archive is the default artwork, not the
authority.

`/system/icons`, `/system/wallpapers` and `/system/fonts` are still created as
real directories, because that is where an override goes and you cannot drop a
file into a directory that does not exist.

---

## 4. Borrowing, and when it is safe

`image_load()` borrows only when the VFS node does **not** own its data
(`data_owned == false`), which today means memory linked into the kernel image
and therefore immortal. A heap-backed node can be rewritten or deleted under a
borrowed pointer, so those are copied.

Archive lookups always borrow: `.rodata` outlives everything.

---

## 5. The cache

`gui/resource_cache.c` — 96 entries, flat array, linear scan, 8 MiB budget.

Ownership, stated plainly because it is the part that bites:

* The cache **owns** every `image_t` it returns. They carry `IMG_CACHED`, so
  `image_free()` on one is a no-op and cannot corrupt the table.
* A returned pointer is valid until the next eviction. **Fetch-and-draw in the
  same frame is always safe** — nothing touched during the current tick is
  evictable, which is why the launcher needs no refcounting at all.
* Holding a pointer **across** frames needs `res_retain()` / `res_release()`.
  The wallpaper does this; an 8 MB decode should not be redone because a 48px
  icon wanted the slot.

Failures are cached too, as a NULL entry. A missing icon is looked up exactly
as often as a present one, and re-walking the VFS sixteen times a frame to
rediscover that a file is still absent costs the same as the problem the cache
was built to solve.

`res_image_sized()` goes through `res_image()`, so several sizes of one asset
share a single decode. When the native size already matches it returns the
native entry *without* also filing it under the sized key — two keys pointing
at one `image_t` would make `res_forget()` free it twice.

**Call `res_image()`, not `res_image_sized()`, when you already know the asset
is the right size.** Because the 1:1 case is never filed under the sized key,
asking `res_image_sized()` for a size the art already is misses *every call*
and pays a full table scan before landing on the `res_image()` hit underneath.
`gui/icon.c` knows the dimensions from the theme's size directories and takes
the direct path when they match; before it did, `res` reported **2298 misses
against 2359 hits** on a desktop drawing nothing but exact-size icons. After:
**20 misses** — exactly one cold load per cached image, and nothing repeated.

That is what the `res` command is for. A 49% miss rate is invisible on screen;
the icons were correct the whole time.

---

## 6. Icon resolution

`appdb`'s `icon` field is read three ways, and `carepkg` writes it the same way:

| value | meaning |
|---|---|
| `browser` | a name in the icon theme |
| `icon.cri` | a file the package shipped — rebased to `/apps/<name>/icon.cri` |
| `/system/icons/x.bmp` | an absolute path, used as-is |

"Contains a dot" is the test. Crude, but it is exactly the rule that keeps every
manifest written before packages could ship artwork (`terminal`, `calc`,
`generic`) resolving to its vector glyph.

A theme name resolves in this order:

1. `/system/icons/<size>/<name>.cri` — exact baked size
2. next size **up**, downscaled (sharper than upscaling)
3. next size **down**
4. `/system/icons/<name>.{cri,bmp,tga}` — a loose drop-in

and if none of that resolves, `icon_draw()` falls back to `gfx_draw_icon()`'s
vector glyph. **That fallback is why this could be merged before a single asset
existed**: the desktop looks exactly as it did before, and improves the moment
the theme is baked in.

### Where icons are actually drawn

Four call sites, and they do not all have an appdb entry in hand:

| site | call | identified by |
|---|---|---|
| `launcher.c` app grid | `icon_draw(entry->icon, …)` | appdb `icon` field |
| `wm.c` desktop sidebar | `icon_draw_app(ic->app, …)` | `app_id_t` |
| `wm.c` window titlebar | `icon_draw_app(w->app, …)` | `app_id_t` |
| `taskbar.c` dock | `icon_draw_app(s->app, …)` | `app_id_t` |

`icon_draw_app()` is the bridge for the last three: `icon_name_for_app()` maps
the enum to a theme name, then the normal lookup runs. Without it those three
sites keep drawing vector glyphs no matter how good the theme is — which is
exactly what happened when only the launcher was wired, and it is not obvious
from the outside, because the launcher is the one surface you have to open a
menu to see.

`icon_name_for_app()`'s names are the keys of `ICON_MAP` in `gen-icons.py`.
Change one without the other and that app silently drops back to its glyph.

The `fallback_color` argument is used **only** for the vector fallback. Themed
icons carry their own palette and are drawn unmodified — tinting a full-colour
Papirus glyph to one theme colour would discard the point of having it. The
titlebar therefore no longer dims its icon when unfocused; focus is still shown
by the titlebar text and border colour.

---

## 7. Packages that ship icons

`.care` section bodies are newline-delimited text, so they cannot carry a byte
of value `0x0A`, let alone `0x00`. A new section type fixes that:

```
FILEB64 icon.cri
Q1JJMTAAMAABAQAAnQkAAP8AAAAA0AAAAAAF1KqqBua0rGbtxL668tXR5Pbm4/n78/L+gf78+/8F
---ENDFILE---
```

Base64, decoded in place on install (output is 3 bytes per 4 input characters,
so the write cursor can never overtake the read cursor). A manifest that uses
no `FILEB64` section is byte-for-byte the manifest an older CareOS installed.

`tools/care-pack.py` builds these. It also ships a *text* file as base64 when
that file contains a line starting with `FILE `, `FILEB64 `, `---ENDFILE---` or
`---END---` — such a line would otherwise be parsed as a directive and truncate
the package. Base64 cannot produce a directive, so the payload arrives intact.

```sh
python3 tools/care-pack.py examples/browser --out examples/browser.care \
    --set name=browser --set exec=main --set icon=icon.cri --set category=Internet
```

See `examples/browser/`.

---

## 8. Baking the theme

Sources are ordinary upstream icon themes, not vendored:

```sh
git clone --depth 1 https://github.com/PapirusDevelopmentTeam/papirus-icon-theme
git clone --depth 1 https://github.com/vinceliuice/Tela-icon-theme

pip install -r tools/requirements.txt
python3 tools/gen-icons.py \
    --source papirus-icon-theme --source Tela-icon-theme \
    --out assets/careos-icons.cra
make icons     # bakes and re-links
```

Sources are searched in order; a name no source supplies is left out and keeps
its vector glyph. A partial theme is a working theme.

`ICON_MAP` at the top of `gen-icons.py` maps CareOS tokens to candidate upstream
names, several per token — themes disagree about these names constantly, and the
second candidate is often the difference between real art and a fallback.

Two things that are less obvious than they look:

**Symlink stubs.** Icon themes lean on symlinks (`web-browser.svg` →
`internet-web-browser.svg`). Git on a host without symlink support writes the
*link target* as the file's contents, so a 24-byte "SVG" containing a filename
is not corruption — it is the link. `resolve_stub()` follows those by hand, so
the script behaves identically on Windows and Linux.

**Alpha out of a renderer that has none.** `reportlab`'s `renderPM` composites
onto a solid background and returns RGB. Rendering twice, on white and on
black, inverts that exactly: for a source pixel `(C, a)`, `Cw = C*a + 255*(1-a)`
and `Cb = C*a`, so `a = 255 - (Cw - Cb)` and `C = Cb/a`. Not an approximation.
It is what makes the baker work on a machine with no cairo libraries at all.

`cairosvg` is preferred when it imports cleanly. Do not install `cairocffi`
alongside `rlPyCairo` on a host without a system cairo — `rlPyCairo` prefers it
and then fails at import. See the note in `tools/requirements.txt`.

---

## 8a. Wallpapers

Six slots, matching the six swatches the Settings → Personalize picker has
always drawn. `settings_get()->wallpaper` selects one; `wallpaper_draw()` reads
it, so choosing a background applies immediately with no reboot.

> That picker was **dead** before this work: it called `settings_set_wallpaper()`
> but nothing read the value when painting, and `draw_wallpaper()` in `gui/wm.c`
> — the function that would have consumed it — has no callers at all. It is
> still there, still unreferenced; the six procedural designs in it were ported
> to the baker instead, so the shipped set matches what it always implied.

`tools/gen-wallpaper.py` is **stdlib-only** — setting a background should not
require installing an SVG rasteriser or Pillow. It decodes **PNG** (8-bit
RGB/RGBA/grey/palette, non-interlaced, via `zlib` plus scanline un-filtering)
and **BMP** (the same 24/32-bpp uncompressed subset `gui/image_bmp.c` accepts),
and writes the `.cri`/`.cra` containers directly.

```sh
python3 tools/gen-wallpaper.py --image ~/pic.png --slot 2   # your own image
python3 tools/gen-wallpaper.py --generate-set               # all six built-ins
python3 tools/gen-wallpaper.py --remove --slot 3            # clear one slot
python3 tools/gen-wallpaper.py --remove-all
make                                                        # re-link
```

Each slot writes two entries: `wallpapers/<N>.cri` for the desktop and
`wallpapers/<N>-thumb.cri` at 132x64 for the picker. **The thumbnails are the
reason the previews are affordable** — six full-size backgrounds decoded at once
would be ~21 MB against an 8 MB cache budget, while six baked thumbs are ~200 KB
total and blit straight into the swatch. Only the *active* background is ever
decoded at full size, and it is pinned; switching slots releases the previous.

Slot 0 is also written as `wallpapers/default.cri`, which any slot with no
artwork falls back to — so a build that bakes only one background shows it
everywhere rather than dropping five slots to the procedural gradient.

### Size is the thing to watch

The archive is linked **into the kernel**, so every byte of wallpaper is a byte
of `kernel.elf`. RLE only helps art with long constant runs:

| source | pixels | raw | in archive |
|---|---|---|---|
| generated gradient, 1280x720 | 0.9 M | 3.5 MiB | **~40 KiB** (1%) |
| `mainbg.png`, 1920x1080 | 2.1 M | 8.1 MiB | **2.1 MiB** (27%) |
| `bg2.png`, 1920x1080 | 2.1 M | 8.1 MiB | **4.1 MiB** (52%) |
| `bg3.png`, 1920x1080 | 2.1 M | 8.1 MiB | **5.0 MiB** (63%) |

Those three took `kernel.elf` from 17 MB to 30 MB and the ISO from 22 MB to
36 MB. That is the honest price of full-resolution photographic wallpapers in a
kernel-embedded archive, and it is why the tool prints the encoded size.

To trade detail for size, drop the ceiling — `--image` is box-filtered down to
fit `--size` (area-average, not nearest; a photo reduced by nearest sampling
aliases badly). `--native` keeps the original:

```sh
python3 tools/gen-wallpaper.py --image bg3.png --slot 2 --size 1280x720
```

A 1280x720 background still fills 1080p because it is cover-fitted at draw time;
it is simply softer. Roughly 2.2x fewer pixels, and RLE does slightly better on
the smoother result.

Deflate would compress these far better — the source PNGs are 0.9–2.1 MB — but
that needs an inflate implementation in the kernel, which is the same reason
there is no PNG decoder (§12).

### Cache budget

`RES_BUDGET_BYTES` is sized against the **largest single image**, not the total.
A 1920x1080 wallpaper is 8.1 MB decoded and the active one is pinned, so at the
original 8 MiB budget one wallpaper exceeded the entire allowance and could
never be evicted: every subsequent icon insert ran the eviction loop, discarded
other icons to make room that pinning made unreclaimable, and re-decoded them
the next frame. The cache would have silently become a thrash-per-frame decoder
while still drawing perfectly correct output.

It is 32 MiB now. Measured after cycling three full-size backgrounds:
`22 images, 24379 KiB, 5984 hits, 22 misses` — one cold load per image, nothing
repeated. Raise it, never lower it, if larger artwork ships.

When no wallpaper entry exists at all, `wallpaper_draw()` returns false and
`draw_elite_wallpaper()` paints its procedural gradient exactly as before.

## 9. Resource directory

```
/system/
    icons/
        16/  24/  32/  48/  64/     terminal.cri, browser.cri, folder.cri, ...
        <name>.{cri,bmp,tga}        loose drop-ins, override the theme
    wallpapers/
        default.{cri,bmp,tga}       cover-fitted; absent => procedural gradient
    fonts/                          reserved; fonts are still C tables
```

Everything under `icons/<size>/` comes from the mounted archive and has no VFS
node. Anything you write there yourself does, and wins.

---

## 10. API

```c
image_t *image_load(const char *path);          /* sniffs .cri / .bmp / .tga  */
image_t *image_load_mem(const u8 *d, u32 n, bool copy_always);
image_t *image_create(u32 w, u32 h);
image_t *image_scaled(const image_t *src, u32 w, u32 h);
void     image_free(image_t *img);

void gfx_draw_image        (image_t *, i32 x, i32 y);
void gfx_draw_image_scaled (image_t *, i32 x, i32 y, i32 w, i32 h);
void gfx_draw_image_alpha  (image_t *, i32 x, i32 y, u8 opacity);
void gfx_draw_image_tinted (image_t *, i32 x, i32 y, u32 tint);
void gfx_draw_image_region (image_t *, i32 sx,i32 sy,i32 sw,i32 sh, i32 dx,i32 dy);
void gfx_draw_image_cover  (image_t *, i32 x, i32 y, i32 w, i32 h);

image_t *res_image(const char *path);                    /* cached            */
image_t *res_image_sized(const char *path, u32 w, u32 h);
void     res_retain(image_t *), res_release(image_t *);
void     res_forget(const char *path);                   /* after overwriting */

image_t *icon_lookup(const char *icon, u32 size);
void     icon_draw(const char *icon, i32 x, i32 y, i32 size,
                   app_id_t fallback, u32 fallback_color);
bool     wallpaper_draw(i32 x, i32 y, i32 w, i32 h);
```

Every drawing entry point honours the active `gfx` clip rect and `g_target`, so
they work unchanged inside a window buffer, and each marks its own dirty
rectangle like the primitives beside them.

`blit_begin()` resolves the target and clip **once per draw** into a bounds box;
the inner loops then clamp against plain integers. That is the difference
between an affordable and an unaffordable full-screen wallpaper blit at 1080p.

Blending is exact `/255` with two shifts and two adds — no divide in the inner
loop, no gamma handling (CareOS composites in sRGB space throughout, like the
rest of `gfx.c`).

---

## 11. Diagnosing it

`resources_init()` resolves one icon at boot and says so:

```
[res] image cache ready
[res] mounted archive: 145 entries under /system
[res] icon theme live (terminal@48 decoded)
```

Mounting an archive only proves the header parsed. The second line proves a
*name* reached a *decoded image*. Without it, a theme that mounts but resolves
nothing looks identical at the console to no theme at all — and because the
fallback is a working vector glyph, nothing on screen says so either.

If you see the WARNING instead, the entry names in the archive do not match what
`gui/icon.c` builds; dump the table with
`python3 -c` against `assets/careos-icons.cra` and compare.

---

## 12. Known limits

* `gfx_flip()` has an SSE2 path that copies `d->w / 4` pixels per dirty row and
  drops the remainder — it would leave a 1–3px stale column on the odd-width
  dirty rects image draws produce. It is **currently dead code**: the kernel
  builds with `-mno-sse2`, so `__SSE2__` is undefined and the correct `kmemcpy`
  path runs. Fix the truncation before ever enabling SSE2.
* No PNG. It needs an inflate implementation; `.cri` exists precisely so the
  build machine does the compression thinking instead.
* Scaling is nearest-neighbour. Icons are baked at their display size, so this
  only runs off-theme, and sharp beats blurred at 48px.
* `image_scaled()` and the cache do not share a size ladder — asking for many
  arbitrary sizes of one big image will hold each of them.
* The `.care` format still cannot carry a text line beginning with a directive;
  `care-pack.py` works around it by encoding such files, but a hand-written
  manifest can still trip on it.
