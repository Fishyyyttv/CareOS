# CareOS v9 — Remote-Render Browser + ≥30 fps Performance Floor

**Date:** 2026-07-28
**Status:** Approved design, ready for implementation planning.

## Goal

Two related deliverables:

1. **Remote-render browser** — make CareOS render *real* modern websites (full
   HTML/CSS/JS, including Next.js/Vite/React SPAs) cleanly and interactively, by
   turning the CareOS browser into a thin client for a headless Chromium running
   on the user's host. CareOS sends the URL and input events; Chromium renders
   the real page and returns a picture of the viewport.
2. **Performance floor** — guarantee the desktop holds **≥30 fps almost always**,
   including the worst case (several windows open, dragging, a remote page image
   updating), by caching per-window content surfaces and refining flip/pacing.

### Why not a native engine
Next.js/Vite sites ship a near-empty HTML page plus a JS bundle that builds the
DOM at runtime. Rendering them requires a full JS engine + DOM + CSS cascade &
layout + compositing — i.e. a browser engine (millions of lines). That is not
achievable in a freestanding hobby OS. Remote rendering (the Opera Mini / Puffin
model) is the only realistic path to clean modern-site rendering, so that is what
we build. The existing native text-flow renderer is **kept** as the offline mode.

---

## Component 1 — Remote-Render Browser

### Architecture

```
CareOS browser (guest)                    Proxy (user's host)
┌──────────────────────┐   HTTP/GET      ┌─────────────────────────┐
│ app_browser "remote" │ ──────────────▶ │ Node + Playwright       │
│  - send url/click/…  │   QOI image     │  headless Chromium      │
│  - decode QOI        │ ◀────────────── │  one page per session   │
│  - blit to content   │                 │  renders REAL JS/CSS    │
│  - forward input     │                 │  → screenshot → QOI     │
└──────────────────────┘                 └─────────────────────────┘
```

CareOS keeps its window/tabs/URL-bar chrome. Only the *content rendering* is
delegated. The viewport Chromium renders is exactly the CareOS content-rect size,
so a content pixel maps 1:1 to a page pixel (trivial input math). Scrolling is
server-side, so CareOS only ever receives one viewport-sized image, never a giant
full-page image.

### Transport & protocol

Plain HTTP GET (no TLS — the proxy is the user's own host/LAN). Every
render-producing endpoint returns a **QOI image** of the current viewport.

| Endpoint | Params | Returns |
|---|---|---|
| `GET /new`    | `w,h`                | text: session id (sid) |
| `GET /nav`    | `s,url(enc),w,h`     | QOI viewport |
| `GET /click`  | `s,x,y`              | QOI viewport |
| `GET /scroll` | `s,dy`               | QOI viewport |
| `GET /key`    | `s,k` (text or named special) | QOI viewport |
| `GET /back` `/fwd` `/reload` | `s`   | QOI viewport |

- `sid` ties a CareOS tab to one Chromium page. A tab lazily calls `/new` on first
  navigation; the sid is stored per window/tab.
- Named special keys for `/key`: `enter`, `back` (backspace), `tab`, `up`, `down`,
  `left`, `right`, `esc`; printable text passed url-encoded.
- Error responses: proxy returns a small QOI error card (or HTTP 5xx); client
  shows the offline/error panel.

### Image format — QOI

QOI (Quite OK Image) is lossless, keeps text crisp, and has a ~100-line decoder
(vs ~1,500 for baseline JPEG). The host↔guest NAT link is fast (local), so QOI's
larger-than-JPEG size is acceptable. Proxy encodes QOI; CareOS decodes it.

- New file `gui/image_qoi.c` + `gui/image_qoi.h`: `qoi_decode(buf, len, out_rgba,
  out_w, out_h, max_bytes)` → decodes into a caller-provided buffer. Bounds-checked
  against `max_bytes`; rejects images larger than the target buffer.
- QOI spec: 14-byte header (`qoif`, width, height, channels, colorspace) + chunks
  (INDEX/DIFF/LUMA/RUN/RGB/RGBA) + 8-byte end marker. Standard, well-documented.

### Net-layer addition

Current `http_get` is text-oriented with a 128 KB buffer. Add a binary-safe fetch:

```c
/* Returns body length (>=0) or -1. Skips HTTP headers, honours Content-Length,
 * reads the full body into buf (up to maxlen). Plaintext HTTP/1.0, Connection:
 * close. Reuses the existing socket/TCP path. */
int http_get_binary(const char *host, u16 port, const char *path,
                    u8 *buf, u32 maxlen);
```

The decoded RGBA image buffer is sized to the **maximum content-rect** (a
fullscreen browser at 1920×1080 gives a content-rect of ≈1900×950 ≈ 7.2 MB, so
allocate for the full-screen bound ~8 MB once, reused across navigations). The
remote viewport is always the current content-rect (≤ that bound), so the buffer
never overflows. The QOI response body buffer is separate and sized to hold one
encoded viewport (~1–2 MB typical; cap ~4 MB, reject larger).

### CareOS client changes (`apps/app_browser.c`)

- Per-window state: `bool browser_remote;` (default true for http/https),
  `char browser_sid[24];`, a decoded-image buffer + `img_w,img_h`, and the proxy
  address (from settings).
- `app_browser_navigate`: when remote, ensure a sid (`/new` if none), build
  `/nav?s=..&w=..&h=..&url=<enc>`, call `http_get_binary` to the proxy, `qoi_decode`
  into the window image buffer. On failure → offline panel + native-render fallback.
- `app_browser_draw`: when remote and an image is present, blit it into the content
  rect 1:1 (no local scroll — server scrolls). Otherwise fall through to the
  existing native `render_html` path.
- Input (remote mode): content-area click → `/click?x=&y=`; mouse wheel →
  `/scroll?dy=`; key → `/key?k=`. Each swaps in the returned image and requests a
  redraw. URL-bar editing and chrome stay local.
- Config: proxy host:port in settings (default `10.0.2.2:8787`), editable; a small
  toggle to force native mode.
- Fallback: proxy unreachable → clear "Remote renderer offline — start the proxy
  on your host (see docs)" panel; native renderer remains available.

### Proxy (`tools/remote-browser/server.js` + `package.json`)

- Raw Node `http` server, `Map<sid,{page}>` over one headless Chromium
  (`playwright`). `sharp` converts the PNG screenshot → raw RGBA for QOI encoding.
- `/nav`: `page.setViewportSize({width:w,height:h})`; `page.goto(url,{waitUntil:
  'load', timeout:15000})`; screenshot → RGBA → QOI → send.
- `/click|/scroll|/key`: drive `page.mouse.click(x,y)` / `page.mouse.wheel(0,dy)` /
  `page.keyboard.*`, then re-screenshot → QOI.
- Idle-session GC (close pages after N minutes). Binds `0.0.0.0:8787`.
- QOI encoder: ~40-line function in JS (encode from RGBA).
- README with `npm install` + `npx playwright install --with-deps chromium` +
  `node server.js`.

**Target environment: proxy in WSL2, CareOS in VirtualBox.** The guest sees the
Windows host at `10.0.2.2` (VirtualBox NAT), but the proxy runs in WSL2 which is a
separate network. Reachability chain and one-time setup:

0. **Disk caveat (this machine):** `C:` is essentially full and heavy WSL writes
   can break the toolchain. Playwright's Chromium is ~150–300 MB — installing it
   into the default WSL cache (`~/.cache/ms-playwright`, on the `C:`-hosted WSL
   VHD) risks filling `C:`. Redirect the browser download to `D:` first:
   `export PLAYWRIGHT_BROWSERS_PATH=/mnt/d/careos-playwright` before
   `npx playwright install chromium`, and set the same var when running
   `server.js`. (Or run the proxy on Windows, pointing Playwright's cache at `D:`.)
1. In WSL2: `node server.js` binds `0.0.0.0:8787`. WSL2's localhost forwarding
   makes it reachable at Windows `127.0.0.1:8787` automatically.
2. On Windows (admin PowerShell), bridge the external port to it once:
   ```
   netsh interface portproxy add v4tov4 listenaddress=0.0.0.0 listenport=8787 ^
       connectaddress=127.0.0.1 connectport=8787
   New-NetFirewallRule -DisplayName "CareOS proxy 8787" -Direction Inbound ^
       -LocalPort 8787 -Protocol TCP -Action Allow
   ```
3. VirtualBox guest reaches the host's `0.0.0.0:8787` at `10.0.2.2:8787` (the
   CareOS default). No VBox port-forward rule needed (that direction is for
   host→guest).

Alternative (no portproxy): run the proxy on the Windows host directly (Node for
Windows + Playwright) so it binds `0.0.0.0:8787` and the guest reaches
`10.0.2.2:8787` with no bridge. The CareOS proxy address is configurable, so
either layout works.

### Phased build

1. **Pipeline** — proxy `/new`+`/nav`; `gui/image_qoi.c`; `http_get_binary`;
   display a navigated page as a static image. Acceptance: a real Next.js/Vite site
   renders cleanly as an image in CareOS.
2. **Interactivity** — `/click`, `/scroll`, `/key`; full click/scroll/type
   browsing. Acceptance: can click links, scroll, and type into a search box on a
   real site and see updated renders.
3. **Polish** — loading indicator, error/offline panel, proxy address in Settings,
   `/back`/`/fwd`/`/reload`, per-tab sessions.

---

## Component 2 — ≥30 fps Performance Floor

### Current state
Backdrop cache (already landed) took the idle/interaction desktop from ~36→~110
fps at 1920×1080 by caching the composited wallpaper + frosted chrome. Remaining
per-frame cost is **`wm_draw_all` (~240 Mcyc)** — every open window re-renders its
full content every frame even when unchanged.

### Plan

1. **Per-window surface cache.** Each window renders its content into its own
   backing buffer. `wm_draw_all` blits those buffers (cheap) and re-renders a
   window's content only when it actually changed: text appended (terminal),
   image swapped (remote browser), content edited, or the window resized. A
   `win->content_dirty` flag drives re-render; move/raise/lower/focus do **not**
   dirty content (only position/z changes, handled by the compositor). This is the
   main lever — dragging windows and idle frames become nearly free regardless of
   window count.
2. **Dirty-rect flips.** Stop forcing `dirty_full` on frames where only part of the
   screen changed, so `gfx_flip` copies less than the full 8 MB. The dirty-rect
   infrastructure already exists in `gfx.c`; feed it real rects from the window
   compositor and cursor instead of blanket full-screen invalidation.
3. **Frame pacing.** Cap the main loop to ~60 fps (skip redraw / short idle when a
   frame finishes early and nothing changed) so it never spin-burns a core; ensures
   headroom rather than runaway CPU.

### Acceptance test (the 30 fps floor)
Re-measure with the existing cycle-counter / fps harness under the **worst case**:
Terminal + Files + remote Browser open, dragging a window while the browser image
updates. Requirement: sustained **≥30 fps** (≤33 ms/frame). Record before/after
numbers. "Almost always 30 fps" = this worst-case scenario measured at ≥30 fps.

---

## Non-goals / explicit scope limits
- No native JS engine, no native CSS layout engine. Modern-site fidelity comes
  from the remote Chromium, not from CareOS.
- No TLS to the proxy (it is the user's own host); TLS remains for direct native
  fetches.
- Remote viewport is capped to the content-rect size (no full-page images).
- The native text-flow renderer is retained only as the offline fallback; it is not
  extended in this project.

## Language choice (C vs C++)
C++ is available to reach for **per-hotspot**, not adopted wholesale up front.
Rationale: with remote rendering the performance-critical layout/paint runs in
Chromium (already C++) on the host; the CareOS-side code is thin-client glue (QOI
decode, one image blit, input forwarding) where C is already optimal. The ≥30 fps
compositor work is memory-bandwidth-bound blit/blend loops that compile
identically in C and C++ — the win is doing less work (caching, dirty rects), not
the language. Reach for C++ only if a specific measured CareOS-side hotspot or a
genuinely complex chrome subsystem (tab/history/session management) justifies it;
the cost is a one-time freestanding-C++ toolchain change (`-fno-exceptions
-fno-rtti -nostdlib`, global-constructor wiring, no STL by default). Default: keep
new client code in C; revisit at the first hotspot that warrants it.

## Risks
- **Host reachability** (guest→host at 10.0.2.2, WSL vs Windows-host proxy) — the
  proxy address is configurable and documented; if the default fails the user
  points it at the right address.
- **Latency** — each interaction is a round trip + render + transfer + decode.
  Acceptable for this use; the per-window cache keeps the *rest* of the desktop
  smooth while a remote fetch is in flight (run the fetch without blocking the
  compositor where feasible).
- **Per-window cache correctness** — stale content if a `content_dirty` trigger is
  missed. Mitigation: conservative dirtying (any input/content call dirties), plus
  a low-rate forced refresh, verified against the fps acceptance test.
