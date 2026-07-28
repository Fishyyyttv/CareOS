# CareOS Remote-Render Browser — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the CareOS browser render real modern websites (full JS/CSS, incl. Next.js/Vite) by acting as a thin client to a headless-Chromium proxy that returns QOI images of the viewport, with click/scroll/type forwarded back.

**Architecture:** A Node+Playwright proxy on the host drives one Chromium page per session; CareOS sends GET requests (`/new,/nav,/click,/scroll,/key,…`) and receives a QOI image of the content-rect-sized viewport, decodes it, and blits it. Server-side scrolling means CareOS only ever handles one viewport image. The native text-flow renderer stays as the offline fallback.

**Tech Stack:** C (freestanding, CareOS side), Node.js + Playwright + sharp (proxy, host side), QOI image format, plaintext HTTP/1.0 over the existing CareOS TCP stack.

## Global Constraints

- Proxy default address: `10.0.2.2:8787` (VirtualBox NAT host address), configurable in CareOS.
- Transport: plaintext HTTP GET only; no TLS to the proxy. Responses are raw QOI bytes (render endpoints) or plain text (`/new`).
- Image format: **QOI** (Quite OK Image), lossless. CareOS decodes; proxy encodes.
- Viewport = current browser content-rect; content pixel maps 1:1 to page pixel. Scrolling is server-side.
- CareOS decoded-image buffer is sized once to the full-screen bound (~1920×1080×4 ≈ 8 MB), reused across navigations. QOI response buffer capped at 4 MB (reject larger).
- Language: C for CareOS-side code. (C++ only per-hotspot, not in this plan.)
- Disk caveat: `C:` is near-full; set `PLAYWRIGHT_BROWSERS_PATH=/mnt/d/careos-playwright` before installing/using Playwright in WSL so Chromium does not land on the `C:`-hosted WSL VHD.
- Build: `wsl.exe -d Ubuntu -- bash -lc "cd /mnt/d/Users/mhetm/Downloads/CareOS_v9_full/CareOS_v9 && make"`.
- Native renderer (`render_html`) is retained untouched as the offline fallback; do not delete or modify its behavior.

## File Structure

| File | Create/Modify | Responsibility |
|------|---------------|----------------|
| `tools/remote-browser/server.js` | CREATE | Node+Playwright proxy: sessions, nav/input, screenshot→QOI |
| `tools/remote-browser/qoi-encode.js` | CREATE | RGBA→QOI encoder (pure function) |
| `tools/remote-browser/package.json` | CREATE | Proxy deps (playwright, sharp) |
| `tools/remote-browser/README.md` | CREATE | Setup (WSL2 + VirtualBox reachability, disk caveat) |
| `tools/remote-browser/test-qoi.js` | CREATE | Node round-trip test for the encoder |
| `gui/image_qoi.h` | CREATE | QOI decoder public API |
| `gui/image_qoi.c` | CREATE | QOI decoder implementation |
| `tests/host/test_qoi.c` | CREATE | Host gcc round-trip test for the decoder |
| `net/net.c` | MODIFY | Add `http_get_binary()` |
| `include/kernel.h` | MODIFY | Declare `http_get_binary()` |
| `gui/gui.h` | MODIFY | `window_t` remote-browser fields |
| `apps/app_browser.c` | MODIFY | Remote mode: navigate/draw/input via proxy; offline fallback |
| `kernel/settings.c`, `include/kernel.h` | MODIFY | Persist proxy host:port |
| `Makefile` | MODIFY | Add `gui/image_qoi.c` to C_SRC; host-test target |

---

## Phase 1 — Rendering pipeline (static navigation)

### Task 1: QOI encoder in the proxy (pure function + Node test)

**Files:**
- Create: `tools/remote-browser/qoi-encode.js`
- Create: `tools/remote-browser/test-qoi.js`
- Create: `tools/remote-browser/package.json`

**Interfaces:**
- Produces: `qoiEncode(rgba: Buffer, width: number, height: number) => Buffer` (channels=4, colorspace=0). QOI stream: 14-byte header `qoif`|w(be32)|h(be32)|channels(1)|colorspace(1), then chunks, then 8-byte end marker `00 00 00 00 00 00 00 01`.

- [ ] **Step 1: Write package.json**

```json
{
  "name": "careos-remote-browser",
  "version": "1.0.0",
  "private": true,
  "type": "commonjs",
  "scripts": { "start": "node server.js", "test": "node test-qoi.js" },
  "dependencies": { "playwright": "^1.47.0", "sharp": "^0.33.0" }
}
```

- [ ] **Step 2: Write the failing Node test**

```js
// test-qoi.js
const assert = require('assert');
const { qoiEncode } = require('./qoi-encode');
// 2x2 image, distinct pixels, then decode-check header + size sanity.
const w = 2, h = 2;
const rgba = Buffer.from([
  255,0,0,255,   0,255,0,255,
  0,0,255,255,   255,255,255,255,
]);
const out = qoiEncode(rgba, w, h);
assert.strictEqual(out.slice(0,4).toString('ascii'), 'qoif', 'magic');
assert.strictEqual(out.readUInt32BE(4), w, 'width');
assert.strictEqual(out.readUInt32BE(8), h, 'height');
assert.strictEqual(out[12], 4, 'channels');
assert.deepStrictEqual([...out.slice(-8)], [0,0,0,0,0,0,0,1], 'end marker');
console.log('qoi-encode OK, bytes =', out.length);
```

- [ ] **Step 3: Run it, verify failure**

Run: `cd tools/remote-browser && node test-qoi.js`
Expected: FAIL — `Cannot find module './qoi-encode'`.

- [ ] **Step 4: Implement the encoder**

```js
// qoi-encode.js — reference QOI encoder (RGBA in, QOI out)
const QOI_OP_INDEX=0x00, QOI_OP_DIFF=0x40, QOI_OP_LUMA=0x80, QOI_OP_RUN=0xc0;
const QOI_OP_RGB=0xfe, QOI_OP_RGBA=0xff;
function hash(r,g,b,a){ return (r*3 + g*5 + b*7 + a*11) & 63; }
function qoiEncode(rgba, width, height){
  const maxSize = width*height*5 + 14 + 8;
  const out = Buffer.alloc(maxSize); let p=0;
  out.write('qoif',p,'ascii'); p+=4;
  out.writeUInt32BE(width,p); p+=4;
  out.writeUInt32BE(height,p); p+=4;
  out[p++]=4; out[p++]=0;
  const index = new Array(64).fill(0).map(()=>({r:0,g:0,b:0,a:0}));
  let pr=0,pg=0,pb=0,pa=255, run=0;
  const px = width*height;
  for(let i=0;i<px;i++){
    const r=rgba[i*4], g=rgba[i*4+1], b=rgba[i*4+2], a=rgba[i*4+3];
    if(r===pr&&g===pg&&b===pb&&a===pa){
      run++;
      if(run===62||i===px-1){ out[p++]=QOI_OP_RUN|(run-1); run=0; }
    } else {
      if(run>0){ out[p++]=QOI_OP_RUN|(run-1); run=0; }
      const h=hash(r,g,b,a);
      const ip=index[h];
      if(ip.r===r&&ip.g===g&&ip.b===b&&ip.a===a){
        out[p++]=QOI_OP_INDEX|h;
      } else {
        index[h]={r,g,b,a};
        if(a===pa){
          const vr=(r-pr+2)|0, vg=(g-pg+2)|0, vb=(b-pb+2)|0;
          const vgg=(g-pg)|0, vrg=(r-pr-vgg+8)|0, vbg=(b-pb-vgg+8)|0;
          if(vr>=0&&vr<4&&vg>=0&&vg<4&&vb>=0&&vb<4){
            out[p++]=QOI_OP_DIFF|(vr<<4)|(vg<<2)|vb;
          } else if(vrg>=0&&vrg<16&&vbg>=0&&vbg<16&&(vgg+32)>=0&&(vgg+32)<64){
            out[p++]=QOI_OP_LUMA|(vgg+32);
            out[p++]=(vrg<<4)|vbg;
          } else { out[p++]=QOI_OP_RGB; out[p++]=r; out[p++]=g; out[p++]=b; }
        } else { out[p++]=QOI_OP_RGBA; out[p++]=r; out[p++]=g; out[p++]=b; out[p++]=a; }
      }
    }
    pr=r; pg=g; pb=b; pa=a;
  }
  for(let k=0;k<7;k++) out[p++]=0; out[p++]=1;
  return out.slice(0,p);
}
module.exports = { qoiEncode };
```

- [ ] **Step 5: Run test, verify pass**

Run: `node test-qoi.js`  → Expected: `qoi-encode OK, bytes = …`.

- [ ] **Step 6: Commit**

```bash
git add tools/remote-browser/qoi-encode.js tools/remote-browser/test-qoi.js tools/remote-browser/package.json
git commit -m "feat(proxy): QOI encoder + node round-trip test"
```

---

### Task 2: QOI decoder in CareOS (host unit test first)

**Files:**
- Create: `gui/image_qoi.h`, `gui/image_qoi.c`
- Create: `tests/host/test_qoi.c`
- Modify: `Makefile` (host-test target + add image_qoi.c to C_SRC)

**Interfaces:**
- Produces: `int qoi_decode(const u8 *in, u32 in_len, u8 *out_rgba, u32 out_cap, u32 *out_w, u32 *out_h);` — returns 0 on success (fills `out_rgba` with w*h*4 BGRA-or-RGBA bytes, sets *out_w/*out_h), -1 on malformed input or if w*h*4 > out_cap. Pixel byte order must match what `app_browser` blits (store as 0x00RRGGBB u32 per pixel — see Step 4; the decoder writes u32 pixels, not raw RGBA, to match gfx).

- [ ] **Step 1: Write the header**

```c
/* gui/image_qoi.h — QOI decoder (qoiformat.org spec) */
#ifndef IMAGE_QOI_H
#define IMAGE_QOI_H
#ifndef HOST_TEST
#include "kernel.h"   /* provides u8/u32; under HOST_TEST the .c typedefs them first */
#endif
/* Decode QOI bytes into a u32 pixel buffer (0x00RRGGBB per pixel, w*h entries).
 * out_cap is the buffer size in BYTES. Returns 0 on success, -1 on error. */
int qoi_decode(const u8 *in, u32 in_len, u32 *out_px, u32 out_cap,
               u32 *out_w, u32 *out_h);
#endif
```

- [ ] **Step 2: Write the host test (fails: no implementation)**

```c
/* tests/host/test_qoi.c — compiled with host gcc, not the kernel toolchain.
 * Provides a tiny known QOI stream and checks the decoded pixels. */
#include <stdint.h>
#include <string.h>
#include <stdio.h>
typedef uint8_t u8; typedef uint32_t u32;
int qoi_decode(const u8*, u32, u32*, u32, u32*, u32*);
/* 1x1 red via QOI_OP_RGB */
int main(void){
  u8 s[] = {'q','o','i','f', 0,0,0,1, 0,0,0,1, 4,0,
            0xfe,255,0,0, /* RGB red */
            0,0,0,0,0,0,0,1};
  u32 px[4]={0}, w=0,h=0;
  int rc = qoi_decode(s,sizeof(s),px,sizeof(px),&w,&h);
  if(rc!=0){ printf("FAIL rc=%d\n",rc); return 1; }
  if(w!=1||h!=1){ printf("FAIL dims %u %u\n",w,h); return 1; }
  if((px[0]&0xFFFFFF)!=0xFF0000){ printf("FAIL px=0x%06x\n",px[0]&0xFFFFFF); return 1; }
  printf("qoi_decode OK\n"); return 0;
}
```

- [ ] **Step 3: Add host-test target to Makefile and run (verify fail)**

Add to `Makefile`:
```make
.PHONY: host-test
host-test:
	gcc -Iinclude -Igui -o /tmp/test_qoi tests/host/test_qoi.c gui/image_qoi.c \
	    -DHOST_TEST 2>&1 || true
	/tmp/test_qoi
```
Run: `wsl.exe -d Ubuntu -- bash -lc "cd <repo> && make host-test"`
Expected: FAIL — link error (no `qoi_decode`) or assertion fail.

Note: `image_qoi.c` must compile under host gcc — it uses only `u8/u32` and pointer math (no kernel calls). Guard any kernel-only includes with `#ifndef HOST_TEST`; define `u8/u32` via `#include "kernel.h"` normally, but for HOST_TEST the test file typedefs them and image_qoi.c must not include kernel.h. Use: at top of image_qoi.c, `#ifdef HOST_TEST` typedef u8/u32 locally `#else` `#include "kernel.h"` `#endif`.

- [ ] **Step 4: Implement the decoder**

```c
/* gui/image_qoi.c */
#ifdef HOST_TEST
typedef unsigned char u8; typedef unsigned int u32; typedef unsigned long long u64;
#else
#include "kernel.h"
#endif
#include "image_qoi.h"

#define QOI_OP_INDEX 0x00
#define QOI_OP_DIFF  0x40
#define QOI_OP_LUMA  0x80
#define QOI_OP_RUN   0xc0
#define QOI_OP_RGB   0xfe
#define QOI_OP_RGBA  0xff
#define QOI_MASK2    0xc0

static u32 rd32(const u8 *p){ return ((u32)p[0]<<24)|((u32)p[1]<<16)|((u32)p[2]<<8)|p[3]; }

int qoi_decode(const u8 *in, u32 in_len, u32 *out_px, u32 out_cap,
               u32 *out_w, u32 *out_h){
    if(!in || in_len < 22) return -1;
    if(in[0]!='q'||in[1]!='o'||in[2]!='i'||in[3]!='f') return -1;
    u32 w = rd32(in+4), h = rd32(in+8);
    if(w==0||h==0||w>4096||h>4096) return -1;
    u32 npx = w*h;
    if((u64)npx*4u > (u64)out_cap) return -1;   /* out buffer too small */
    u8 r=0,g=0,b=0,a=255;
    u8 idx[64*4]; for(int i=0;i<64*4;i++) idx[i]=0;
    u32 p = 14;           /* after header */
    u32 end = in_len - 8; /* before 8-byte end marker */
    u32 run = 0, out_i = 0;
    while(out_i < npx){
        if(run > 0){ run--; }
        else if(p < end){
            u8 op = in[p++];
            if(op==QOI_OP_RGB){ if(p+3>end) return -1; r=in[p++]; g=in[p++]; b=in[p++]; }
            else if(op==QOI_OP_RGBA){ if(p+4>end) return -1; r=in[p++]; g=in[p++]; b=in[p++]; a=in[p++]; }
            else if((op&QOI_MASK2)==QOI_OP_INDEX){ int h4=(op&63)*4; r=idx[h4]; g=idx[h4+1]; b=idx[h4+2]; a=idx[h4+3]; }
            else if((op&QOI_MASK2)==QOI_OP_DIFF){ r+=((op>>4)&3)-2; g+=((op>>2)&3)-2; b+=(op&3)-2; }
            else if((op&QOI_MASK2)==QOI_OP_LUMA){ if(p>=end) return -1; u8 b2=in[p++]; int vg=(op&63)-32; r+=vg+((b2>>4)&15)-8; g+=vg; b+=vg+(b2&15)-8; }
            else if((op&QOI_MASK2)==QOI_OP_RUN){ run=(op&63); }
            int hh=((r*3+g*5+b*7+a*11)&63)*4; idx[hh]=r; idx[hh+1]=g; idx[hh+2]=b; idx[hh+3]=a;
        } else return -1;
        out_px[out_i++] = ((u32)r<<16)|((u32)g<<8)|b;   /* 0x00RRGGBB */
    }
    *out_w = w; *out_h = h;
    return 0;
}
```

(`u64` is defined in `kernel.h` (this is a 64-bit OS) and under `HOST_TEST` add `typedef unsigned long long u64;` next to the u8/u32 typedefs so the check compiles in both builds.)

- [ ] **Step 5: Run host test, verify pass**

Run: `make host-test` → Expected: `qoi_decode OK`.

- [ ] **Step 6: Add image_qoi.c to kernel build**

In `Makefile` `C_SRC`, add `gui/image_qoi.c \` after `gui/image_tga.c \`.
Run: `make` → Expected: clean build.

- [ ] **Step 7: Commit**

```bash
git add gui/image_qoi.h gui/image_qoi.c tests/host/test_qoi.c Makefile
git commit -m "feat(gui): QOI image decoder + host round-trip test"
```

---

### Task 3: Binary HTTP GET in the net layer

**Files:**
- Modify: `net/net.c` (add `http_get_binary`)
- Modify: `include/kernel.h` (declare it)

**Interfaces:**
- Produces: `int http_get_binary(const char *host, u16 port, const char *path, u8 *buf, u32 maxlen);` — connects, sends `GET path HTTP/1.0` with `Host`, `Connection: close`; reads the full response, locates the `\r\n\r\n` header/body split, copies the body (honoring `Content-Length` when present, else read-to-close) into `buf` up to `maxlen`. Returns body length (>=0) or -1. Sets `net_error_buf` on failure via existing `net_set_error`.

- [ ] **Step 1: Add prototype to kernel.h**

Under the existing networking prototypes (near `int http_get(...)`), add:
```c
int http_get_binary(const char *host, u16 port, const char *path, u8 *buf, u32 maxlen);
```

- [ ] **Step 2: Implement in net.c**

After `http_get(...)` in `net/net.c`, add:
```c
int http_get_binary(const char *host, u16 port, const char *path, u8 *buf, u32 maxlen){
    u32 ip;
    net_error_buf[0] = '\0';
    if (dns_resolve(host, &ip) != 0) { net_set_error("DNS lookup failed", host); return -1; }
    int sock = sock_create(SOCK_TCP);
    if (sock < 0) { net_set_error("No sockets available", host); return -1; }
    if (sock_connect(sock, ip, port) != 0) { sock_close(sock); net_set_error("TCP connect failed", host); return -1; }

    char req[512];
    kstrcpy(req, "GET ");
    kstrcat(req, path);
    kstrcat(req, " HTTP/1.0\r\nHost: ");
    kstrcat(req, host);
    kstrcat(req, "\r\nUser-Agent: CareOS/9\r\nConnection: close\r\n\r\n");
    sock_send(sock, (const u8*)req, (u32)kstrlen(req));

    /* Read whole response into a temp region of buf's tail is not safe; read
     * directly and track the header split. We accumulate into buf, then shift
     * the body to the front once the split is found. */
    u32 got = 0; u8 rb[1024]; int n;
    while ((n = sock_recv(sock, rb, sizeof(rb))) > 0 && got < maxlen) {
        u32 c = (u32)n; if (got + c > maxlen) c = maxlen - got;
        kmemcpy(buf + got, rb, c); got += c;
    }
    sock_close(sock);
    if (got == 0) { net_set_error("No response", host); return -1; }

    /* Find header/body split. */
    u32 hs = 0; bool found = false;
    for (u32 i = 0; i + 3 < got; i++)
        if (buf[i]=='\r'&&buf[i+1]=='\n'&&buf[i+2]=='\r'&&buf[i+3]=='\n'){ hs = i+4; found = true; break; }
    if (!found) { net_set_error("No HTTP header", host); return -1; }
    u32 blen = got - hs;
    for (u32 i = 0; i < blen; i++) buf[i] = buf[hs + i];  /* shift body to front */
    return (int)blen;
}
```

- [ ] **Step 3: Build, verify clean**

Run: `make` → Expected: clean build (no new warnings).

- [ ] **Step 4: Commit**

```bash
git add net/net.c include/kernel.h
git commit -m "feat(net): http_get_binary for binary bodies (images)"
```

---

### Task 4: Proxy `/new` + `/nav` (static render end-to-end)

**Files:**
- Create: `tools/remote-browser/server.js`
- Create: `tools/remote-browser/README.md`

**Interfaces:**
- Consumes: `qoiEncode` (Task 1).
- Produces: HTTP server on `:8787`. `GET /new?w=&h=` → `text/plain` sid. `GET /nav?s=&url=&w=&h=` → `application/octet-stream` QOI bytes.

- [ ] **Step 1: Write server.js**

```js
// tools/remote-browser/server.js
const http = require('http');
const { URL } = require('url');
const { chromium } = require('playwright');
const sharp = require('sharp');
const { qoiEncode } = require('./qoi-encode');

const PORT = 8787;
let browser;
const sessions = new Map(); // sid -> { page, last }

async function ensureBrowser(){ if(!browser) browser = await chromium.launch({ headless:true }); }
async function newSession(w,h){
  await ensureBrowser();
  const ctx = await browser.newContext({ viewport:{ width:w, height:h } });
  const page = await ctx.newPage();
  const sid = Math.random().toString(36).slice(2,10);
  sessions.set(sid, { page, ctx, last: Date.now() });
  return sid;
}
async function shot(page,w,h){
  await page.setViewportSize({ width:w, height:h });
  const png = await page.screenshot({ type:'png' });
  const { data, info } = await sharp(png).ensureAlpha().raw().toBuffer({ resolveWithObject:true });
  return qoiEncode(data, info.width, info.height);
}
function sendQoi(res, buf){ res.writeHead(200,{'Content-Type':'application/octet-stream','Content-Length':buf.length}); res.end(buf); }
function sendErr(res, code, msg){ res.writeHead(code,{'Content-Type':'text/plain'}); res.end(msg); }

const server = http.createServer(async (req,res)=>{
  try{
    const u = new URL(req.url, 'http://x');
    const q = u.searchParams;
    const w = Math.min(parseInt(q.get('w')||'1024'),1920);
    const h = Math.min(parseInt(q.get('h')||'768'),1080);
    if(u.pathname==='/new'){ const sid=await newSession(w,h); return sendErr(res,200,sid); }
    const sid = q.get('s'); const s = sessions.get(sid);
    if(!s) return sendErr(res,404,'no session');
    s.last = Date.now();
    if(u.pathname==='/nav'){
      const url = q.get('url'); if(!url) return sendErr(res,400,'no url');
      await s.page.goto(url, { waitUntil:'load', timeout:15000 }).catch(()=>{});
      return sendQoi(res, await shot(s.page,w,h));
    }
    return sendErr(res,404,'unknown');
  }catch(e){ sendErr(res,500, String(e)); }
});
// Idle GC
setInterval(()=>{ const now=Date.now(); for(const [sid,s] of sessions){ if(now-s.last>300000){ s.ctx.close().catch(()=>{}); sessions.delete(sid);} } }, 60000);
server.listen(PORT, '0.0.0.0', ()=> console.log('CareOS remote-browser proxy on :'+PORT));
```

- [ ] **Step 2: Write README.md**

Include: `export PLAYWRIGHT_BROWSERS_PATH=/mnt/d/careos-playwright`, `npm install`, `npx playwright install chromium`, `node server.js`; the `netsh portproxy` bridge (WSL2→Windows→VBox at `10.0.2.2:8787`) exactly as in the design spec; and a `curl` smoke test.

- [ ] **Step 3: Manual smoke test (host)**

Run (WSL, browsers on D:):
```bash
cd tools/remote-browser
export PLAYWRIGHT_BROWSERS_PATH=/mnt/d/careos-playwright
npm install && npx playwright install chromium
node server.js &
SID=$(curl -s "http://127.0.0.1:8787/new?w=1024&h=768")
curl -s "http://127.0.0.1:8787/nav?s=$SID&w=1024&h=768&url=https%3A%2F%2Fexample.com" -o /tmp/out.qoi
head -c4 /tmp/out.qoi   # expect: qoif
```
Expected: `/tmp/out.qoi` begins with `qoif` and is non-trivial in size.

- [ ] **Step 4: Commit**

```bash
git add tools/remote-browser/server.js tools/remote-browser/README.md
git commit -m "feat(proxy): /new + /nav render endpoints (Playwright→QOI)"
```

---

### Task 5: CareOS remote navigation + display (Phase 1 integration)

**Files:**
- Modify: `gui/gui.h` (window_t fields)
- Modify: `apps/app_browser.c` (remote navigate + draw)

**Interfaces:**
- Consumes: `qoi_decode` (Task 2), `http_get_binary` (Task 3), proxy `/new`,`/nav` (Task 4).
- Produces: remote navigation path that fills `w->browser_img_px` (u32 pixels) + `img_w/img_h`, drawn into the content-rect.

- [ ] **Step 1: Add window_t fields (gui.h)**

After the existing browser fields in `window_t`:
```c
    /* Remote-render browser */
    bool  browser_remote;         /* true = proxy mode (default) */
    char  browser_sid[16];        /* proxy session id, "" if none */
    u32  *browser_img_px;         /* decoded viewport (0x00RRGGBB), NULL until first nav */
    u32   browser_img_w, browser_img_h;
    bool  browser_remote_error;   /* proxy unreachable → show offline panel + native */
```

- [ ] **Step 2: Add remote statics + proxy address (top of app_browser.c)**

```c
#include "image_qoi.h"
/* Proxy config (Task 8 wires this to settings; default for now). */
static char g_proxy_host[64] = "10.0.2.2";
static u16  g_proxy_port     = 8787;
/* Response + decode buffers (sized to full-screen bound; reused). */
static u8   g_qoi_buf[4u*1024u*1024u];              /* QOI response body */
static u32  g_img_store[1920u*1080u];               /* decoded pixels    */
```

- [ ] **Step 3: Implement remote navigate helper**

```c
/* Returns 0 on success (fills w->browser_img_*), -1 on any failure. */
static int browser_remote_nav(window_t *w, const char *url, i32 vw, i32 vh) {
    char path[600], enc[512];
    url_encode(url, enc, sizeof(enc));
    /* Ensure a session. */
    if (!w->browser_sid[0]) {
        char np[64]; ksprintf(np, "/new?w=%d&h=%d", (int)vw, (int)vh);
        int n = http_get_binary(g_proxy_host, g_proxy_port, np, g_qoi_buf, sizeof(g_qoi_buf));
        if (n <= 0 || n >= (int)sizeof(w->browser_sid)) return -1;
        kmemcpy(w->browser_sid, g_qoi_buf, (u32)n); w->browser_sid[n] = '\0';
    }
    ksprintf(path, "/nav?s=%s&w=%d&h=%d&url=%s", w->browser_sid, (int)vw, (int)vh, enc);
    int n = http_get_binary(g_proxy_host, g_proxy_port, path, g_qoi_buf, sizeof(g_qoi_buf));
    if (n <= 0) return -1;
    u32 dw=0, dh=0;
    if (qoi_decode(g_qoi_buf, (u32)n, g_img_store, sizeof(g_img_store), &dw, &dh) != 0) return -1;
    w->browser_img_px = g_img_store; w->browser_img_w = dw; w->browser_img_h = dh;
    return 0;
}
```

- [ ] **Step 4: Branch app_browser_navigate to remote for http/https**

In `app_browser_navigate`, before the existing HTTPS/HTTP native blocks, add:
```c
    if (w->browser_remote && (kstrncmp(url,"http://",7)==0 || kstrncmp(url,"https://",8)==0)) {
        rect_t cr = wm_client_rect(w);
        i32 vw = cr.w, vh = cr.h - BROW_BAR_H - BROW_STAT_H;   /* content area */
        if (browser_remote_nav(w, url, vw, vh) == 0) {
            w->browser_remote_error = false;
            kstrncpy(w->browser_url, url, 255);
            w->browser_loading = false;
            ksprintf(g_status, "%s  (remote)", url);
            history_push(w, url);
            return;
        }
        w->browser_remote_error = true;      /* fall through to native fallback */
    }
```

- [ ] **Step 5: Blit the remote image in app_browser_draw**

In `app_browser_draw`, at the top of the content-drawing section (before the native `render_html` call), add:
```c
    if (w->browser_remote && !w->browser_remote_error && w->browser_img_px) {
        rect_t cr = wm_client_rect(w);
        i32 cx = cr.x, cy = cr.y + BROW_BAR_H;
        i32 iw = (i32)w->browser_img_w, ih = (i32)w->browser_img_h;
        for (i32 y = 0; y < ih; y++) {
            u32 *src = &w->browser_img_px[(u32)y * w->browser_img_w];
            for (i32 x = 0; x < iw; x++) gfx_px(cx + x, cy + y, src[x]);
        }
        return;   /* skip native render */
    }
    if (w->browser_remote_error) {
        rect_t cr = wm_client_rect(w);
        gfx_str(cr.x + 16, cr.y + BROW_BAR_H + 16,
                "Remote renderer offline. Start the proxy on your host (see docs).",
                COL_RED, COL_TRANSPARENT);
        /* then fall through to native render below */
    }
```
(Use the existing fast blit primitive if one exists — e.g. a row `kmemcpy` into the backbuffer via `gfx_blit_image`; `gfx_px` per-pixel is the correctness-first version. Task in Perf plan optimizes this to a row blit.)

- [ ] **Step 6: Initialise remote fields in app_browser_init**

In `app_browser_init`, add:
```c
    w->browser_remote = true;
    w->browser_sid[0] = '\0';
    w->browser_img_px = 0; w->browser_img_w = w->browser_img_h = 0;
    w->browser_remote_error = false;
```

- [ ] **Step 7: Build + integration test (VirtualBox screenshot)**

Run `make`. With the proxy running and reachable at `10.0.2.2:8787`, boot in VirtualBox (per build/test workflow), open Browser, navigate to a real Next.js site (e.g. `https://nextjs.org`). Capture `VBoxManage controlvm <vm> screenshotpng`.
Expected: the real site renders as an image in the content area. If the proxy is down, the offline panel shows and native render still works.

- [ ] **Step 8: Commit**

```bash
git add gui/gui.h apps/app_browser.c
git commit -m "feat(browser): remote-render navigation + viewport display (phase 1)"
```

---

## Phase 2 — Interactivity

### Task 6: Proxy input endpoints (`/click`, `/scroll`, `/key`, `/back`, `/fwd`, `/reload`)

**Files:**
- Modify: `tools/remote-browser/server.js`

**Interfaces:**
- Produces: each endpoint drives the page then returns a fresh QOI viewport.

- [ ] **Step 1: Add handlers before the `/nav` return in server.js**

```js
    if(u.pathname==='/click'){ const x=+q.get('x')||0,y=+q.get('y')||0;
      await s.page.mouse.click(x,y).catch(()=>{}); await s.page.waitForTimeout(150);
      return sendQoi(res, await shot(s.page,w,h)); }
    if(u.pathname==='/scroll'){ const dy=+q.get('dy')||0;
      await s.page.mouse.wheel(0,dy).catch(()=>{}); await s.page.waitForTimeout(80);
      return sendQoi(res, await shot(s.page,w,h)); }
    if(u.pathname==='/key'){ const k=q.get('k')||'';
      const named={enter:'Enter',back:'Backspace',tab:'Tab',up:'ArrowUp',down:'ArrowDown',left:'ArrowLeft',right:'ArrowRight',esc:'Escape'};
      if(named[k]) await s.page.keyboard.press(named[k]).catch(()=>{});
      else await s.page.keyboard.type(k).catch(()=>{});
      await s.page.waitForTimeout(60);
      return sendQoi(res, await shot(s.page,w,h)); }
    if(u.pathname==='/back'){ await s.page.goBack().catch(()=>{}); return sendQoi(res, await shot(s.page,w,h)); }
    if(u.pathname==='/fwd'){ await s.page.goForward().catch(()=>{}); return sendQoi(res, await shot(s.page,w,h)); }
    if(u.pathname==='/reload'){ await s.page.reload().catch(()=>{}); return sendQoi(res, await shot(s.page,w,h)); }
```

- [ ] **Step 2: Smoke test**

```bash
curl -s "http://127.0.0.1:8787/scroll?s=$SID&dy=400" -o /tmp/s.qoi && head -c4 /tmp/s.qoi
```
Expected: `qoif`.

- [ ] **Step 3: Commit**

```bash
git add tools/remote-browser/server.js
git commit -m "feat(proxy): click/scroll/key/back/fwd/reload endpoints"
```

---

### Task 7: CareOS forwards input in remote mode

**Files:**
- Modify: `apps/app_browser.c` (click, scroll, key handlers)

**Interfaces:**
- Consumes: proxy input endpoints (Task 6). Reuses `browser_remote_nav`'s decode path via a shared `browser_remote_request(w, path)` helper.

- [ ] **Step 1: Extract a shared request→decode helper**

Refactor Task 5's nav so both nav and input reuse:
```c
static int browser_remote_request(window_t *w, const char *path) {
    int n = http_get_binary(g_proxy_host, g_proxy_port, path, g_qoi_buf, sizeof(g_qoi_buf));
    if (n <= 0) { w->browser_remote_error = true; return -1; }
    u32 dw=0, dh=0;
    if (qoi_decode(g_qoi_buf,(u32)n,g_img_store,sizeof(g_img_store),&dw,&dh)!=0) { w->browser_remote_error=true; return -1; }
    w->browser_img_px=g_img_store; w->browser_img_w=dw; w->browser_img_h=dh;
    w->browser_remote_error=false; return 0;
}
```
(Have `browser_remote_nav` call it for the `/nav` leg.)

- [ ] **Step 2: Forward clicks in app_browser_click**

In the content-area branch of `app_browser_click`, when `w->browser_remote && !error`:
```c
    rect_t cr = wm_client_rect(w);
    i32 px = mx - cr.x, py = my - (cr.y + BROW_BAR_H);
    if (px >= 0 && py >= 0 && py < (i32)w->browser_img_h) {
        char path[64]; ksprintf(path, "/click?s=%s&x=%d&y=%d", w->browser_sid, (int)px, (int)py);
        browser_remote_request(w, path);
        return;
    }
```

- [ ] **Step 3: Forward wheel scroll**

In `app_browser` scroll handling (mouse wheel), remote branch:
```c
    char path[64]; ksprintf(path, "/scroll?s=%s&dy=%d", w->browser_sid, (int)(-scroll_delta*120));
    browser_remote_request(w, path);
```

- [ ] **Step 4: Forward keys in app_browser_key**

When `w->browser_remote` and the URL bar is NOT focused:
```c
    char path[80]; char kb[8];
    if (c=='\n') kstrcpy(kb,"enter"); else if (c=='\b') kstrcpy(kb,"back");
    else if (c=='\t') kstrcpy(kb,"tab"); else { kb[0]=c; kb[1]='\0'; }
    ksprintf(path, "/key?s=%s&k=%s", w->browser_sid, kb);
    browser_remote_request(w, path);
    return;
```
(Printable chars are sent literally; url_encode them if `c` can be a reserved char — wrap `kb` through `url_encode` for safety.)

- [ ] **Step 5: Build + interactive test (VirtualBox)**

Boot in VBox with the proxy up; navigate to a search engine, click the box, type a query, press Enter, scroll results. Screenshot after each.
Expected: clicks/typing/scroll reflect in the returned images.

- [ ] **Step 6: Commit**

```bash
git add apps/app_browser.c
git commit -m "feat(browser): forward click/scroll/key to remote session (phase 2)"
```

---

## Phase 3 — Polish

### Task 8: Proxy address in Settings + loading indicator + back/fwd/reload buttons

**Files:**
- Modify: `kernel/settings.c`, `include/kernel.h` (persist `proxy_host`, `proxy_port`)
- Modify: `apps/app_browser.c` (read settings; loading status; wire existing nav buttons to `/back`,`/fwd`,`/reload`)

**Interfaces:**
- Consumes: `settings_get()`; proxy back/fwd/reload endpoints.

- [ ] **Step 1: Add settings fields**

In `careos_settings_t` (include/kernel.h) add `char proxy_host[64]; u16 proxy_port;`. In `kernel/settings.c` defaults, set `"10.0.2.2"` / `8787`. Add `settings_set_proxy(const char*, u16)` mirroring existing setters.

- [ ] **Step 2: Load into app_browser on init**

In `app_browser_init`, replace the hardcoded defaults:
```c
    const careos_settings_t *s = settings_get();
    if (s && s->proxy_host[0]) { kstrncpy(g_proxy_host, s->proxy_host, sizeof(g_proxy_host)-1); g_proxy_port = s->proxy_port ? s->proxy_port : 8787; }
```

- [ ] **Step 3: Loading indicator**

Set `g_status = "Loading… (remote)"` before each `browser_remote_request`, and the final status after. Draw a small spinner/"Loading" in the status bar while `w->browser_loading` is true (set true before the request, false after).

- [ ] **Step 4: Wire back/fwd/reload buttons (remote)**

In `app_browser_click` for the existing `<`,`>`,`R` buttons, when remote:
```c
    char path[48]; ksprintf(path, "/%s?s=%s", which, w->browser_sid); /* which = back|fwd|reload */
    browser_remote_request(w, path);
```

- [ ] **Step 5: Build + test**

Change proxy address in Settings to a bad value → offline panel; set it back → works. Back/fwd/reload navigate the remote session.

- [ ] **Step 6: Commit**

```bash
git add kernel/settings.c include/kernel.h apps/app_browser.c
git commit -m "feat(browser): proxy address in settings, loading state, nav buttons"
```

---

## Self-Review Notes (coverage)
- Spec "protocol table" → Tasks 4, 6. "QOI" → Tasks 1, 2. "http_get_binary" → Task 3.
- "remote mode in app_browser / fallback" → Tasks 5, 7. "config in settings" → Task 8.
- "server-side scroll / 1:1 mapping" → Tasks 5–7. "native renderer retained" → Task 5 fallthrough.
- Deferred to the Perf plan (called out in Task 5 Step 5): replacing per-pixel `gfx_px` blit with a row blit, and running the remote fetch without stalling the compositor.

## Open follow-ups (Perf plan territory, not here)
- Non-blocking remote fetch (so the desktop stays ≥30 fps while a nav is in flight).
- Row-blit the decoded image instead of per-pixel.
