#!/usr/bin/env python3
"""Put wallpapers into the CareOS resource archive. No dependencies.

Deliberately stdlib-only, unlike tools/gen-icons.py. Setting a desktop
background should not require installing an SVG rasteriser, so this decodes PNG
and BMP itself and writes the .cri and .cra containers directly.

  # replace the background in Settings slot 0
  python3 tools/gen-wallpaper.py --image ~/pic.png --slot 0

  # bake the full six-slot set the Settings picker offers
  python3 tools/gen-wallpaper.py --generate-set

  # drop one slot, or all of them
  python3 tools/gen-wallpaper.py --remove --slot 3
  python3 tools/gen-wallpaper.py --remove-all

Then re-link with `make`.

Each slot produces two entries: "wallpapers/<N>.cri" for the desktop and
"wallpapers/<N>-thumb.cri" at the Settings swatch size. The thumbnail exists so
the picker can show six previews for ~200 KiB total instead of decoding six
full-size backgrounds (which would be ~5 MiB and blow the image cache budget).

Slot 0 is also written as "wallpapers/default.cri", which is what
gui/icon.c falls back to when the selected slot has no image.
"""
import argparse
import struct
import sys
import zlib
from pathlib import Path

CRI_MAGIC = b"CRI1"
CRI_ENC_BGRA32, CRI_ENC_RLE = 0, 1
CRI_HEADER_BYTES = 16

CRA_MAGIC = b"CRA1"
CRA_HEADER_BYTES = 16
CRA_ENTRY_BYTES = 60
CRA_NAME_MAX = 48

WALLPAPER_SLOTS = 6          # must match the picker in apps/app_settings.c
THUMB_W, THUMB_H = 132, 64   # the swatch size that picker draws
DEFAULT_SIZE = (1280, 720)


# ── Surface: a flat BGRA bytearray plus dimensions ──────────────────────────
class Surface:
    def __init__(self, w, h, data=None):
        self.w, self.h = w, h
        self.px = data if data is not None else bytearray(w * h * 4)

    def set(self, x, y, rgb):
        if 0 <= x < self.w and 0 <= y < self.h:
            i = (y * self.w + x) * 4
            self.px[i:i + 4] = bytes((rgb[2], rgb[1], rgb[0], 255))

    def row_fill(self, y, rgb):
        self.px[y * self.w * 4:(y + 1) * self.w * 4] = bytes((rgb[2], rgb[1], rgb[0], 255)) * self.w

    def blend_rect(self, x0, y0, rw, rh, rgb, alpha):
        for y in range(max(0, y0), min(self.h, y0 + rh)):
            for x in range(max(0, x0), min(self.w, x0 + rw)):
                i = (y * self.w + x) * 4
                b, g, r = self.px[i], self.px[i + 1], self.px[i + 2]
                self.px[i] = (b * (255 - alpha) + rgb[2] * alpha) // 255
                self.px[i + 1] = (g * (255 - alpha) + rgb[1] * alpha) // 255
                self.px[i + 2] = (r * (255 - alpha) + rgb[0] * alpha) // 255


def box_resize(src: Surface, w: int, h: int) -> Surface:
    """Area-average downscale, aspect preserved by the caller.

    Nearest-neighbour is fine for the flat generated gradients and for icons
    baked at their display size, but a photograph reduced by nearest sampling
    aliases badly -- fine detail turns into shimmer. Averaging the source box
    behind each destination pixel is a few more lines and looks like a real
    resize.
    """
    if (w, h) == (src.w, src.h):
        return src
    out = Surface(w, h)
    for y in range(h):
        sy0 = y * src.h // h
        sy1 = max(sy0 + 1, (y + 1) * src.h // h)
        for x in range(w):
            sx0 = x * src.w // w
            sx1 = max(sx0 + 1, (x + 1) * src.w // w)
            b = g = r = a = n = 0
            for sy in range(sy0, sy1):
                base = sy * src.w * 4
                for sx in range(sx0, sx1):
                    i = base + sx * 4
                    b += src.px[i]; g += src.px[i+1]; r += src.px[i+2]; a += src.px[i+3]
                    n += 1
            j = (y * w + x) * 4
            out.px[j:j+4] = bytes((b // n, g // n, r // n, a // n))
    return out


def fit_within(src: Surface, w: int, h: int) -> Surface:
    """Downscale to fit inside w*h, preserving aspect. Never upscales."""
    if src.w <= w and src.h <= h:
        return src
    scale = min(w / src.w, h / src.h)
    return box_resize(src, max(1, int(src.w * scale)), max(1, int(src.h * scale)))


def cover_resize(src: Surface, w: int, h: int) -> Surface:
    """Nearest-neighbour cover fit: fill w*h, preserve aspect, crop the excess.

    Same rule gfx_draw_image_cover() applies at draw time, so a thumbnail is
    framed the way the full background will be.
    """
    scale = max(w / src.w, h / src.h)
    sw, sh = src.w * scale, src.h * scale
    ox, oy = (sw - w) / 2, (sh - h) / 2
    out = Surface(w, h)
    for y in range(h):
        sy = min(src.h - 1, int((y + oy) / scale))
        for x in range(w):
            sx = min(src.w - 1, int((x + ox) / scale))
            i = (sy * src.w + sx) * 4
            j = (y * w + x) * 4
            out.px[j:j + 4] = src.px[i:i + 4]
    return out


# ── PNG (stdlib zlib) ───────────────────────────────────────────────────────
def read_png(path: Path) -> Surface:
    """8-bit non-interlaced RGB/RGBA/grey PNG.

    The whole decoder is inflate plus scanline un-filtering, both of which are
    short and exact -- which is why this is worth 60 lines rather than a Pillow
    dependency just to set a desktop background.
    """
    d = path.read_bytes()
    if d[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")

    idat, pos = bytearray(), 8
    w = h = depth = ctype = interlace = None
    palette = None
    while pos + 8 <= len(d):
        ln, = struct.unpack_from(">I", d, pos)
        typ = d[pos + 4:pos + 8]
        body = d[pos + 8:pos + 8 + ln]
        pos += 12 + ln
        if typ == b"IHDR":
            w, h, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
        elif typ == b"PLTE":
            palette = body
        elif typ == b"IDAT":
            idat += body
        elif typ == b"IEND":
            break

    if w is None:
        raise ValueError("PNG has no IHDR")
    if depth != 8:
        raise ValueError(f"{depth}-bit PNG is not supported; save as 8-bit")
    if interlace:
        raise ValueError("interlaced (Adam7) PNG is not supported; save without")
    if ctype not in (0, 2, 3, 6):
        raise ValueError(f"unsupported PNG colour type {ctype}")
    if ctype == 3 and palette is None:
        raise ValueError("paletted PNG with no PLTE chunk")

    channels = {0: 1, 2: 3, 3: 1, 6: 4}[ctype]
    raw = zlib.decompress(bytes(idat))
    stride = w * channels
    if len(raw) < (stride + 1) * h:
        raise ValueError("PNG data is truncated")

    out = Surface(w, h)
    prev = bytearray(stride)
    p = 0
    for y in range(h):
        ft = raw[p]; p += 1
        line = bytearray(raw[p:p + stride]); p += stride
        # Un-filter in place; see RFC 2083 section 6.
        if ft == 1:
            for i in range(channels, stride):
                line[i] = (line[i] + line[i - channels]) & 0xFF
        elif ft == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif ft == 3:
            for i in range(stride):
                a = line[i - channels] if i >= channels else 0
                line[i] = (line[i] + ((a + prev[i]) >> 1)) & 0xFF
        elif ft == 4:
            for i in range(stride):
                a = line[i - channels] if i >= channels else 0
                c = prev[i - channels] if i >= channels else 0
                b = prev[i]
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 0xFF
        elif ft != 0:
            raise ValueError(f"bad PNG filter type {ft} on row {y}")
        prev = line

        o = y * w * 4
        if ctype == 2:
            for x in range(w):
                r, g, b = line[x * 3], line[x * 3 + 1], line[x * 3 + 2]
                out.px[o + x * 4:o + x * 4 + 4] = bytes((b, g, r, 255))
        elif ctype == 6:
            for x in range(w):
                r, g, b, a = line[x * 4:x * 4 + 4]
                out.px[o + x * 4:o + x * 4 + 4] = bytes((b, g, r, a))
        elif ctype == 0:
            for x in range(w):
                v = line[x]
                out.px[o + x * 4:o + x * 4 + 4] = bytes((v, v, v, 255))
        else:  # ctype 3, palette
            for x in range(w):
                e = line[x] * 3
                out.px[o + x * 4:o + x * 4 + 4] = bytes(
                    (palette[e + 2], palette[e + 1], palette[e], 255))
    return out


# ── BMP (mirrors gui/image_bmp.c) ───────────────────────────────────────────
def read_bmp(path: Path) -> Surface:
    d = path.read_bytes()
    if len(d) < 54 or d[:2] != b"BM":
        raise ValueError("not a BMP (no 'BM' signature)")

    off_bits, = struct.unpack_from("<I", d, 10)
    hdr_size, = struct.unpack_from("<I", d, 14)
    if hdr_size < 40:
        raise ValueError(f"unsupported BMP header size {hdr_size} (need >= 40)")

    w, h = struct.unpack_from("<ii", d, 18)
    planes, bpp = struct.unpack_from("<HH", d, 26)
    compression, = struct.unpack_from("<I", d, 30)
    if planes != 1:
        raise ValueError(f"unsupported plane count {planes}")
    if bpp not in (24, 32):
        raise ValueError(f"unsupported {bpp}-bpp BMP; save as 24- or 32-bit")
    if compression not in (0, 3):
        raise ValueError("compressed (RLE) BMP is not supported; save uncompressed")

    top_down = h < 0
    h = abs(h)
    if w <= 0 or h <= 0:
        raise ValueError("bad BMP dimensions")

    src_bpp = bpp // 8
    row_bytes = ((w * src_bpp) + 3) & ~3
    if off_bits + row_bytes * h > len(d):
        raise ValueError("BMP is truncated")

    out = Surface(w, h)
    for y in range(h):
        srow = off_bits + (y if top_down else (h - 1 - y)) * row_bytes
        o = y * w * 4
        for x in range(w):
            s = srow + x * src_bpp
            a = d[s + 3] if src_bpp == 4 else 255
            out.px[o + x * 4:o + x * 4 + 4] = bytes((d[s], d[s + 1], d[s + 2], a))

    # 32-bpp BMPs whose alpha was never written decode fully transparent; same
    # rescue as image_classify_alpha() in gui/image.c.
    if src_bpp == 4 and not any(out.px[i] for i in range(3, len(out.px), 4)):
        for i in range(3, len(out.px), 4):
            out.px[i] = 255
    return out


def read_image(path: Path) -> Surface:
    suffix = path.suffix.lower()
    if suffix == ".png":
        return read_png(path)
    if suffix == ".bmp":
        return read_bmp(path)
    raise ValueError(f"unsupported format '{suffix}'; use .png or .bmp "
                     f"(or bake via gen-icons.py --wallpaper, which uses Pillow)")


# ── The six built-in backgrounds ────────────────────────────────────────────
# Ported from draw_wallpaper() in gui/wm.c so the baked set matches the names
# the Settings picker has always implied.
BUILTIN_NAMES = ("Azure Depth", "Midnight Ridge", "Forest Fog",
                 "Dark Carbon", "Arctic Slate", "Crimson Night")


def generate(slot: int, w: int, h: int) -> Surface:
    s = Surface(w, h)

    def grad(top, bot):
        for y in range(h):
            t = y / max(1, h - 1)
            s.row_fill(y, tuple(int(a + (b - a) * t) for a, b in zip(top, bot)))

    def line(x0, y0, x1, y1, rgb):
        n = max(abs(x1 - x0), abs(y1 - y0), 1)
        for i in range(n + 1):
            s.set(x0 + (x1 - x0) * i // n, y0 + (y1 - y0) * i // n, rgb)

    if slot == 1:      # Midnight Ridge
        grad((0x0f, 0x17, 0x2a), (0x1e, 0x29, 0x3b))
        for d in (0, 1, 2):
            line(0, h * 3 // 4 + d, w, h * 4 // 5 + d,
                 (0x33, 0x41, 0x55) if d < 2 else (0x0f, 0x17, 0x2a))
    elif slot == 2:    # Forest Fog
        grad((0x06, 0x4e, 0x3b), (0x02, 0x2c, 0x22))
        for x in range(0, w, 120):
            s.blend_rect(x, h // 2, 60, h // 2, (0x05, 0x96, 0x69), 26)
    elif slot == 3:    # Dark Carbon
        grad((0x11, 0x11, 0x11), (0x1a, 0x1a, 0x1a))
        for y in range(0, h, 4):
            s.row_fill(y, (0x0a, 0x0a, 0x0a))
    elif slot == 4:    # Arctic Slate
        grad((0x1e, 0x29, 0x3b), (0x0f, 0x17, 0x2a))
        s.blend_rect(w // 2, 0, w // 2, h, (0xff, 0xff, 0xff), 13)
    elif slot == 5:    # Crimson Night
        grad((0x45, 0x0a, 0x0a), (0x11, 0x11, 0x11))
        s.blend_rect(0, h // 2, w, 3, (0x7f, 0x1d, 0x1d), 51)
    else:              # 0 -- Azure Depth, the CareOS default
        grad((0x0B, 0x16, 0x36), (0x04, 0x06, 0x11))

        def tri(p0, p1, p2, rgb):
            pts = sorted([p0, p1, p2], key=lambda p: p[1])
            (x0, y0), (x1, y1), (x2, y2) = pts
            for y in range(max(0, y0), min(h, y2 + 1)):
                if y < y1 and y1 != y0:
                    xa = x0 + (x1 - x0) * (y - y0) // (y1 - y0)
                elif y2 != y1:
                    xa = x1 + (x2 - x1) * (y - y1) // (y2 - y1)
                else:
                    xa = x1
                xb = x0 + (x2 - x0) * (y - y0) // (y2 - y0) if y2 != y0 else x0
                for x in range(min(xa, xb), max(xa, xb) + 1):
                    s.set(x, y, rgb)

        def circ(cx, cy, rad, rgb):
            for y in range(max(0, cy - rad), min(h, cy + rad + 1)):
                dy = y - cy
                if abs(dy) > rad:
                    continue
                sp = int((rad * rad - dy * dy) ** 0.5)
                for x in range(max(0, cx - sp), min(w, cx + sp + 1)):
                    s.set(x, y, rgb)

        tri((w // 4, 0), (w * 2 // 3, 0), (w // 7, h), (0x11, 0x26, 0x5C))
        cx, cy, rad = w * 45 // 100, h * 42 // 100, int(w * 0.30)
        circ(cx, cy, rad, (0x2A, 0x5E, 0xCC))
        circ(cx + rad // 4, cy - rad // 10, rad - max(8, rad // 12), (0x0A, 0x13, 0x2C))
        tri((w // 5, h), (w * 2 // 5, h), (w * 3 // 10, h * 3 // 4), (0x50, 0x3E, 0xC0))
    return s


# ── .cri / .cra containers ──────────────────────────────────────────────────
def rle_encode(raw: bytes) -> bytes:
    out = bytearray()
    n = len(raw) // 4
    i = 0
    while i < n:
        px = raw[i * 4:(i + 1) * 4]
        run = 1
        while run < 128 and i + run < n and raw[(i + run) * 4:(i + run + 1) * 4] == px:
            run += 1
        if run > 1:
            out.append(0x80 | (run - 1)); out += px; i += run
            continue
        start = i
        i += 1
        while i < n and i - start < 128:
            if i + 1 < n and raw[i * 4:(i + 1) * 4] == raw[(i + 1) * 4:(i + 2) * 4]:
                break
            i += 1
        out.append(i - start - 1)
        out += raw[start * 4:i * 4]
    return bytes(out)


def encode_cri(s: Surface) -> bytes:
    raw = bytes(s.px)
    has_alpha = any(raw[i] != 0xFF for i in range(3, len(raw), 4))
    packed = rle_encode(raw)
    enc, payload = (CRI_ENC_RLE, packed) if len(packed) < len(raw) else (CRI_ENC_BGRA32, raw)
    return CRI_MAGIC + struct.pack("<HHBBHI", s.w, s.h, enc,
                                   1 if has_alpha else 0, 0, len(payload)) + payload


def read_cra(path: Path):
    if not path.is_file():
        return []
    b = path.read_bytes()
    if b[:4] != CRA_MAGIC:
        raise ValueError(f"{path} is not a .cra archive")
    count, total, _ = struct.unpack_from("<III", b, 4)
    if total != len(b):
        raise ValueError(f"{path}: header says {total} bytes, file is {len(b)}")
    entries = []
    for i in range(count):
        rec = CRA_HEADER_BYTES + i * CRA_ENTRY_BYTES
        name = b[rec:rec + CRA_NAME_MAX].rstrip(b"\0").decode()
        off, ln, w, h = struct.unpack_from("<IIHH", b, rec + CRA_NAME_MAX)
        entries.append((name, b[off:off + ln], w, h))
    return entries


def write_cra(path: Path, entries):
    for name, *_ in entries:
        if len(name.encode()) >= CRA_NAME_MAX:
            raise ValueError(f"entry name too long: {name}")
    table_bytes = CRA_HEADER_BYTES + len(entries) * CRA_ENTRY_BYTES
    base = (table_bytes + 3) & ~3
    table, blob = bytearray(), bytearray()
    for name, payload, w, h in entries:
        blob += b"\0" * ((-len(blob)) & 3)      # payloads stay 4-byte aligned
        table += name.encode().ljust(CRA_NAME_MAX, b"\0")
        table += struct.pack("<IIHH", base + len(blob), len(payload), w, h)
        blob += payload
    total = base + len(blob)
    header = CRA_MAGIC + struct.pack("<III", len(entries), total, 0)
    path.write_bytes(header + table + b"\0" * (base - table_bytes) + blob)
    return total


def slot_names(slot: int):
    names = [f"wallpapers/{slot}.cri", f"wallpapers/{slot}-thumb.cri"]
    if slot == 0:
        # Not written any more -- listed so re-baking cleans it out of an
        # archive produced by an earlier version of this script.
        names.append("wallpapers/default.cri")
    return names


def install(entries, slot: int, surf: Surface, quiet=False):
    entries[:] = [e for e in entries if e[0] not in slot_names(slot)]
    full = encode_cri(surf)
    thumb = encode_cri(cover_resize(surf, THUMB_W, THUMB_H))

    # Slot 0 used to be duplicated as "wallpapers/default.cri" for gui/icon.c to
    # fall back to. With photographic backgrounds that second copy cost 2.1 MB
    # of kernel image for nothing, so the fallback now points at slot 0 itself.
    # "default.{cri,bmp,tga}" remains meaningful as a loose VFS drop-in.
    entries.append((f"wallpapers/{slot}.cri", full, surf.w, surf.h))
    entries.append((f"wallpapers/{slot}-thumb.cri", thumb, THUMB_W, THUMB_H))
    if not quiet:
        print(f"  slot {slot}: {surf.w}x{surf.h} -> {len(full)/1024:.1f} KiB "
              f"+ {len(thumb)/1024:.1f} KiB thumb")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    mode = ap.add_mutually_exclusive_group(required=True)
    mode.add_argument("--image", type=Path, help="PNG or BMP to install")
    mode.add_argument("--generate", action="store_true",
                      help="built-in background for --slot")
    mode.add_argument("--generate-set", action="store_true",
                      help="bake all six built-in backgrounds")
    mode.add_argument("--remove", action="store_true", help="drop --slot")
    mode.add_argument("--remove-all", action="store_true", help="drop every wallpaper")
    ap.add_argument("--slot", type=int, default=0,
                    help=f"Settings slot 0..{WALLPAPER_SLOTS - 1} (default 0)")
    ap.add_argument("--size", default="%dx%d" % DEFAULT_SIZE,
                    help="WxH for generated backgrounds; also the ceiling an "
                         "--image is downscaled to fit (never upscaled)")
    ap.add_argument("--native", action="store_true",
                    help="keep an --image at its own resolution, ignoring --size")
    ap.add_argument("--archive", type=Path, default=Path("assets/careos-icons.cra"))
    args = ap.parse_args()

    if not (0 <= args.slot < WALLPAPER_SLOTS):
        return f"--slot must be 0..{WALLPAPER_SLOTS - 1}"
    try:
        entries = read_cra(args.archive)
    except ValueError as exc:
        return str(exc)

    if args.remove_all:
        entries = [e for e in entries if not e[0].startswith("wallpapers/")]
        total = write_cra(args.archive, entries)
        print(f"removed all wallpapers; {len(entries)} entries, {total/1024:.1f} KiB")
        print("re-link with: make")
        return 0

    if args.remove:
        entries = [e for e in entries if e[0] not in slot_names(args.slot)]
        total = write_cra(args.archive, entries)
        print(f"removed slot {args.slot}; {len(entries)} entries, {total/1024:.1f} KiB")
        print("re-link with: make")
        return 0

    try:
        w, h = (int(v) for v in args.size.lower().split("x"))
    except ValueError:
        return f"--size wants WxH, got {args.size!r}"
    if not (0 < w <= 4096 and 0 < h <= 4096):
        return "--size must be within 4096x4096 (IMAGE_MAX_DIM in gui/image.h)"

    if args.image:
        if not args.image.is_file():
            return f"no such file: {args.image}"
        try:
            surf = read_image(args.image)
        except ValueError as exc:
            return f"{args.image}: {exc}"
        except zlib.error as exc:
            return f"{args.image}: corrupt PNG data ({exc})"
        print(f"read {args.image.name}: {surf.w}x{surf.h}")
        if not args.native:
            fitted = fit_within(surf, w, h)
            if fitted is not surf:
                print(f"  downscaled to {fitted.w}x{fitted.h} "
                      f"(--native keeps the original, --size changes the ceiling)")
                surf = fitted
        install(entries, args.slot, surf)
    elif args.generate:
        print(f"generating '{BUILTIN_NAMES[args.slot]}' at {w}x{h} ...")
        install(entries, args.slot, generate(args.slot, w, h))
    else:
        print(f"generating all {WALLPAPER_SLOTS} backgrounds at {w}x{h} ...")
        for i in range(WALLPAPER_SLOTS):
            print(f"  [{i}] {BUILTIN_NAMES[i]}")
            install(entries, i, generate(i, w, h), quiet=True)

    total = write_cra(args.archive, entries)
    print(f"{args.archive}: {len(entries)} entries, {total/1024:.1f} KiB")
    print("re-link with: make")
    return 0


if __name__ == "__main__":
    sys.exit(main())
