# CareOS Native Web Engine ("Weave")

**Date:** 2026-10-04
**Status:** Approved direction (user chose: fully self-contained, approach 1 — hybrid
port + own layout; basic JavaScript in scope). Supersedes the remote-render
direction of `2026-07-28-careos-remote-browser-and-perf-design.md` for the browser
(that spec's perf-floor half still stands).

## Goal
Replace the browser's single-pass text-flow renderer (`render_html` in
`apps/app_browser.c`) with a real engine running entirely inside CareOS:
HTML5 parsing into a DOM, the CSS cascade, block/inline/flex/grid/table layout,
painting with real TTF fonts and decoded images, and basic JavaScript.

**Target sites:** static / server-rendered pages (Wikipedia, blogs, docs, news,
Hacker News, search results) render with their real styling; simple scripts
(menus, DOM tweaks, dynamic text) run. Heavy SPA frameworks and media playback
(YouTube) are explicitly out of scope.

## Architecture

```
 fetch (background task)            engine (web/engine, portable C)          UI thread
 ───────────────────────            ─────────────────────────────            ─────────
 HTML bytes ──▶ lexbor HTML5 parse ──▶ DOM ──▶ lexbor cascade (UA+author)
                <link> CSS fetched ──┘          │
                                               ▼
                                  computed style (css.c)  ──▶ box tree (box.c)
                                               ▼
                       layout: block/inline/float (layout.c), flex, grid, table
                                               ▼
                    paint(viewport rect) ──▶ u32 canvas ──▶ gfx_blit_argb (CareOS)
 <img> bytes ──▶ stb_image ──┘        fonts: stb_truetype glyph cache over TTFs
 <script> ──▶ QuickJS-ng + DOM bindings (js.c) ──▶ mutations ──▶ relayout
```

- `web/third_party/`: lexbor (core, tag, ns, dom, html, css, selectors, style),
  stb_image, stb_truetype, stb_image_write (host only), QuickJS-ng (JS phase).
  All MIT / Apache-2.0 / public domain → compatible with CareOS GPLv3.
- `web/engine/`: our code, standard C only (CareOS provides the subset via
  `include/libc` + `kernel/libc_shim.c`). Single public header `web.h`.
- Platform hooks (`web_platform_t`): fetch(url) → bytes, font data, clock, log.
  Host harness implements them with curl/files; CareOS with `browser_fetch` /
  embedded TTFs.
- The engine owns its painting (software rasterizer into a `u32` XRGB canvas),
  so host and CareOS output are pixel-identical. CareOS only blits.

## Units
| File | Purpose |
|---|---|
| `web.h` | API: `web_page_load`, `web_page_layout(width)`, `web_page_paint(canvas, scroll)`, `web_page_hit(x,y)` → link/element, `web_page_height`, `web_page_free` |
| `css.c` | computed style per element: inheritance, initial values, units (px/em/rem/%/pt/vw/vh), colors, shorthands lexbor lacks (`background`, `border-radius`, `gap`, `list-style`, `overflow`, grid templates) |
| `ua.css` (string) | default HTML styles |
| `box.c` | DOM → box tree (block/inline/anonymous, text, replaced, list markers, `display:none` pruning) |
| `layout*.c` | block + inline line breaking + floats; flex; grid (common subset); tables (auto layout); absolute/fixed/relative |
| `font.c` | face selection (sans/serif/mono × weight; synthetic italic), glyph cache, UTF-8 |
| `paint.c` | canvas ops: rect, alpha, rounded rect, borders, text, scaled image, clip; stacking by z-index |
| `image.c` | fetch + stb_image decode, size cap |
| `js.c` | QuickJS runtime, DOM/window bindings, timers, events (JS phase) |

## Phases
1. **Engine core + host harness** — parse, cascade, block/inline layout, paint
   text/boxes; host renders real URLs to PNG.
2. **Layout breadth** — floats, flex, grid, tables, positioning, images.
3. **CareOS integration** — build in freestanding mode, embedded fonts, browser
   app uses engine (load in fetch task, blit, scroll, link clicks); old renderer
   remains as fallback if the engine fails.
4. **Forms + interactivity** — inputs, buttons, GET form submission, hover.
5. **JavaScript** — QuickJS port + DOM bindings.

## Limits / non-goals
No video/audio, no WebGL/canvas 2D (initially), no CSS animations/transitions,
no `@font-face` web fonts (system TTFs only), no non-UTF-8 decoding, no
`calc()` beyond what lexbor parses. Resource caps: HTML ≤ 4 MB, ≤ 16 stylesheets,
≤ 96 images, image ≤ 4096² decoded.

## Testing
Host harness `web/host/webrender` renders URLs/files to PNG; screenshot review
against real sites + a fixture set under `web/host/fixtures/`. CareOS verified in
QEMU/VirtualBox screenshots.
