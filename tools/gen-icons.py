#!/usr/bin/env python3
"""Bake an icon theme into a CareOS .cra resource archive.

Run once on the dev machine and commit the output, exactly like
tools/gen-font.py and tools/gen-sounds.py. The kernel build never invokes this
script -- it only links the archive it produces.

Sources are ordinary upstream icon themes; nothing is vendored into the repo:

  git clone --depth 1 https://github.com/PapirusDevelopmentTeam/papirus-icon-theme
  git clone --depth 1 https://github.com/vinceliuice/Tela-icon-theme

  python3 tools/gen-icons.py \
      --source papirus-icon-theme \
      --source Tela-icon-theme \
      --wallpaper art/wallpaper.png \
      --out assets/careos-icons.cra

Sources are searched in order, so put the theme you prefer first. Any CareOS
icon a source cannot supply falls through to the next one, and a name no source
supplies is simply left out of the archive -- gui/icon.c then falls back to the
vector glyph in gfx_draw_icon(), which is what the desktop drew before this
system existed. A partial theme is a working theme.

Rendering needs an SVG rasteriser. cairosvg is preferred; svglib + reportlab is
used when cairo's native libraries are not installed (the usual case on
Windows). Raster sources (.png) need only Pillow.
"""
import argparse
import os
import struct
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    sys.exit("Pillow is required: pip install -r tools/requirements.txt")

# ── Container constants. Must match gui/image.h and gui/resource_cache.h ─────
CRI_MAGIC = b"CRI1"
CRI_ENC_BGRA32 = 0
CRI_ENC_RLE_BGRA32 = 1
CRI_FLAG_HAS_ALPHA = 0x01
CRI_HEADER_BYTES = 16

CRA_MAGIC = b"CRA1"
CRA_HEADER_BYTES = 16
CRA_ENTRY_BYTES = 60
CRA_NAME_MAX = 48

DEFAULT_SIZES = (16, 24, 32, 48, 64)

# ── What CareOS asks for, and what upstream calls it ────────────────────────
# Keys are the icon tokens that appear in include/appdb.h entries and in .care
# manifests. Values are candidate upstream names tried in order; the first one
# that exists in any source wins. Several candidates per token is not padding --
# themes disagree about these names constantly, and a second candidate is the
# difference between a real icon and a fallback glyph.
ICON_MAP = {
    # Built-in applications (see launcher_icons[] in gui/launcher.c)
    "terminal":  ("utilities-terminal", "gnome-terminal", "xterm"),
    "files":     ("system-file-manager", "nautilus", "folder"),
    "editor":    ("accessories-text-editor", "text-editor", "gedit"),
    "browser":   ("web-browser", "internet-web-browser", "firefox"),
    "netmon":    ("preferences-system-network", "network-wired", "nm-device-wired"),
    "settings":  ("preferences-system", "systemsettings", "preferences-desktop"),
    "sysmon":    ("utilities-system-monitor", "gnome-system-monitor", "htop"),
    "users":     ("system-users", "preferences-system-users", "avatar-default"),
    "packages":  ("system-software-install", "gnome-software", "package-manager"),
    "calc":      ("accessories-calculator", "gnome-calculator", "galculator"),
    "clock":     ("accessories-clock", "gnome-clocks", "preferences-system-time"),
    "notes":     ("accessories-notes", "gnote", "tomboy"),
    "paint":     ("kolourpaint", "gimp", "krita"),
    "about":     ("help-about", "help-browser", "system-help"),
    "help":      ("help-browser", "system-help", "help-contents"),
    "maze":      ("applications-games", "gnome-maze", "org.gnome.Nibbles"),
    "3d":        ("applications-graphics", "blender", "org.blender.Blender"),
    "doom":      ("applications-games", "gnome-boxes", "steam"),
    # Generic / package fallback. gui/launcher.c maps an unknown token here.
    "generic":   ("application-x-executable", "application-default-icon"),

    # File manager furniture. These are addressed by name from app_files.c the
    # same way an app icon is, so they live in the same namespace.
    "folder":            ("folder", "folder-blue", "inode-directory"),
    "folder-documents":  ("folder-documents", "folder-doc", "folder"),
    "folder-download":   ("folder-download", "folder-downloads", "folder"),
    "folder-music":      ("folder-music", "folder-sound", "folder"),
    "folder-pictures":   ("folder-pictures", "folder-image", "folder"),
    "folder-videos":     ("folder-videos", "folder-video", "folder"),
    "user-home":         ("user-home", "folder-home", "folder"),
    "drive-harddisk":    ("drive-harddisk", "harddisk", "drive-removable-media"),
    "text-file":         ("text-x-generic", "text-plain", "text-x-preview"),
    "executable":        ("application-x-executable", "application-x-sharedlib"),
    "image-file":        ("image-x-generic", "image-x-generic-symbolic", "multimedia-photo-manager"),
}

# Directories within a theme that may hold an icon, in search order.
CATEGORIES = ("apps", "places", "mimetypes", "devices", "categories",
              "actions", "status", "panel")


# ── Symlink stubs ───────────────────────────────────────────────────────────
def resolve_stub(path: Path, depth: int = 0) -> Path:
    """Follow a Papirus-style symlink that was checked out as a text file.

    Icon themes lean heavily on symlinks (web-browser.svg -> internet-web-
    browser.svg). Git on a host without symlink support writes the LINK TARGET
    as the file's contents instead, so a 24-byte "svg" containing a filename is
    not corruption -- it is the link, and following it by hand here is what
    makes this script work identically on Windows and Linux.
    """
    if depth > 8:
        raise ValueError(f"symlink loop at {path}")
    try:
        head = path.read_bytes()[:512]
    except OSError:
        return path
    stripped = head.strip()
    if stripped.startswith(b"<") or b"\x00" in head or len(stripped) > 255:
        return path                      # real SVG (or a raster file)
    if not stripped.endswith(b".svg") and not stripped.endswith(b".png"):
        return path
    target = (path.parent / stripped.decode("utf-8", "replace")).resolve()
    if not target.is_file():
        raise ValueError(f"dangling link {path} -> {stripped!r}")
    return resolve_stub(target, depth + 1)


# ── Source discovery ────────────────────────────────────────────────────────
def theme_roots(source: Path):
    """Yield (size_or_None, directory) pairs holding icon category folders.

    Handles both layouts we care about without being told which is which:
      Papirus  <root>/Papirus[-Dark]/48x48/apps/foo.svg
      Tela     <root>/src/scalable/apps/foo.svg  and  <root>/src/24/apps/foo.svg
    A directory that already contains category folders is used as-is, so a
    hand-made source tree works too.
    """
    candidates = [source]
    candidates += sorted(p for p in source.glob("Papirus*") if p.is_dir())
    if (source / "src").is_dir():
        candidates.append(source / "src")

    seen = set()
    for base in candidates:
        if not base.is_dir():
            continue
        for entry in sorted(base.iterdir()):
            if not entry.is_dir():
                continue
            if entry.name in CATEGORIES:
                if base not in seen:
                    seen.add(base)
                    yield (None, base)
                continue
            # "48x48" or "48" -> a size-specific subtree; "scalable" -> any size.
            name = entry.name
            size = None
            if name == "scalable":
                size = 0                       # 0 sorts as "resolution free"
            elif "x" in name and name.split("x")[0].isdigit():
                size = int(name.split("x")[0])
            elif name.isdigit():
                size = int(name)
            if size is not None and entry not in seen:
                seen.add(entry)
                yield (size, entry)


def build_index(sources):
    """index[(source_order, size)][icon_name] = Path, built once up front."""
    index = []
    for order, source in enumerate(sources):
        for size, directory in theme_roots(source):
            table = {}
            for category in CATEGORIES:
                cat_dir = directory / category
                if not cat_dir.is_dir():
                    continue
                for f in cat_dir.iterdir():
                    if f.suffix.lower() in (".svg", ".png") and f.stem not in table:
                        table[f.stem] = f
            if table:
                index.append((order, size, directory, table))
    # Prefer earlier sources; within a source, prefer size-specific over
    # scalable (0) -- Papirus hand-tunes its 24px art and it shows.
    index.sort(key=lambda e: (e[0], 0 if e[1] else 1))
    return index


def find_icon(index, candidates, want_size):
    """Best (path, source_size) for any of `candidates` at `want_size`."""
    best = None
    for name in candidates:
        for order, size, _dir, table in index:
            if name not in table:
                continue
            # Distance from the requested size; scalable (0) is neutral.
            dist = 0 if not size else abs(size - want_size) + (0 if size >= want_size else 1)
            key = (order, dist)
            if best is None or key < best[0]:
                best = (key, table[name], size)
        if best is not None:
            return best[1], best[2]        # first candidate that exists wins
    return None, None


# ── Rasterising ─────────────────────────────────────────────────────────────
_RENDERER = None


def pick_renderer():
    global _RENDERER
    if _RENDERER:
        return _RENDERER
    try:
        import cairosvg                                   # noqa: F401
        _RENDERER = "cairosvg"
    except Exception:
        try:
            from svglib.svglib import svg2rlg             # noqa: F401
            from reportlab.graphics import renderPM       # noqa: F401
            _RENDERER = "svglib"
        except Exception:
            _RENDERER = "none"
    return _RENDERER


def render_cairosvg(path: Path, size: int) -> Image.Image:
    import cairosvg
    import io
    png = cairosvg.svg2png(url=str(path), output_width=size, output_height=size)
    return Image.open(io.BytesIO(png)).convert("RGBA")


def render_svglib(path: Path, size: int) -> Image.Image:
    """Rasterise with svglib, recovering alpha from two opaque renders.

    reportlab's renderPM has no alpha channel: it composites onto a solid
    background and hands back RGB. Rendering the same drawing twice, on white
    and on black, is enough to invert that exactly. For a source pixel (C, a):

        white render  Cw = C*a + 255*(1-a)
        black render  Cb = C*a

    so Cw - Cb = 255*(1-a), giving a = 255 - (Cw - Cb), and C = Cb/a. This is
    exact, not an approximation, and it is the reason this script works on a
    machine with no cairo libraries.
    """
    from svglib.svglib import svg2rlg
    from reportlab.graphics import renderPM

    drawing = svg2rlg(str(path))
    if drawing is None:
        raise ValueError(f"svglib could not parse {path}")
    if drawing.width and drawing.height:
        drawing.scale(size / drawing.width, size / drawing.height)
        drawing.width = drawing.height = size

    white = renderPM.drawToPIL(drawing, bg=0xFFFFFF).convert("RGB")
    black = renderPM.drawToPIL(drawing, bg=0x000000).convert("RGB")
    if white.size != (size, size):
        white = white.resize((size, size), Image.LANCZOS)
        black = black.resize((size, size), Image.LANCZOS)

    wp, bp = white.load(), black.load()
    out = Image.new("RGBA", (size, size))
    op = out.load()
    for y in range(size):
        for x in range(size):
            rw, gw, bw = wp[x, y]
            rb, gb, bb = bp[x, y]
            # Any channel yields the same alpha in theory; take the largest
            # implied alpha so antialiasing noise cannot punch holes in a solid.
            a = 255 - min(rw - rb, gw - gb, bw - bb)
            if a <= 0:
                op[x, y] = (0, 0, 0, 0)
            elif a >= 255:
                op[x, y] = (rb, gb, bb, 255)
            else:
                op[x, y] = (min(255, rb * 255 // a),
                            min(255, gb * 255 // a),
                            min(255, bb * 255 // a), a)
    return out


def rasterise(path: Path, size: int) -> Image.Image:
    path = resolve_stub(path)
    if path.suffix.lower() == ".png":
        img = Image.open(path).convert("RGBA")
        if img.size != (size, size):
            img = img.resize((size, size), Image.LANCZOS)
        return img

    renderer = pick_renderer()
    if renderer == "cairosvg":
        return render_cairosvg(path, size)
    if renderer == "svglib":
        return render_svglib(path, size)
    raise RuntimeError(
        "no SVG renderer available. Install one of:\n"
        "  pip install cairosvg      (needs cairo native libs)\n"
        "  pip install svglib        (pure Python, works everywhere)")


# ── .cri encoding ───────────────────────────────────────────────────────────
def to_bgra(img: Image.Image) -> bytes:
    """RGBA -> the exact byte order gui/image.c reads back as 0xAARRGGBB."""
    r, g, b, a = img.split()
    return Image.merge("RGBA", (b, g, r, a)).tobytes()


def rle_encode(raw: bytes) -> bytes:
    """Byte-oriented RLE over 4-byte pixels; see the format note in image.h.

    Flat-colour icon art compresses hard here (a 64px Papirus folder drops by
    roughly half), and the decoder is twenty lines. Runs are capped at 128
    pixels because the count shares a byte with the packet-type flag.
    """
    out = bytearray()
    n = len(raw) // 4
    i = 0
    while i < n:
        px = raw[i * 4:(i + 1) * 4]
        run = 1
        while run < 128 and i + run < n and raw[(i + run) * 4:(i + run + 1) * 4] == px:
            run += 1
        if run > 1:
            out.append(0x80 | (run - 1))
            out += px
            i += run
            continue
        # Literal packet: gather until a pair repeats, which is where a run
        # packet would start paying for itself.
        start = i
        i += 1
        while i < n and i - start < 128:
            if i + 1 < n and raw[i * 4:(i + 1) * 4] == raw[(i + 1) * 4:(i + 2) * 4]:
                break
            i += 1
        count = i - start
        out.append(count - 1)
        out += raw[start * 4:i * 4]
    return bytes(out)


def encode_cri(img: Image.Image, allow_rle: bool = True) -> bytes:
    w, h = img.size
    if w > 0xFFFF or h > 0xFFFF:
        raise ValueError("image too large for the .cri header")
    raw = to_bgra(img)
    has_alpha = any(raw[i] != 0xFF for i in range(3, len(raw), 4))

    encoding, payload = CRI_ENC_BGRA32, raw
    if allow_rle:
        packed = rle_encode(raw)
        if len(packed) < len(raw):
            encoding, payload = CRI_ENC_RLE_BGRA32, packed

    header = CRI_MAGIC + struct.pack(
        "<HHBBHI", w, h, encoding,
        CRI_FLAG_HAS_ALPHA if has_alpha else 0, 0, len(payload))
    assert len(header) == CRI_HEADER_BYTES
    return header + payload


# ── .cra archive ────────────────────────────────────────────────────────────
def build_cra(entries):
    """entries: list of (name, payload_bytes, width, height)."""
    for name, *_ in entries:
        if len(name.encode("utf-8")) >= CRA_NAME_MAX:
            raise ValueError(f"entry name too long for the archive: {name}")

    table_bytes = CRA_HEADER_BYTES + len(entries) * CRA_ENTRY_BYTES
    offset = (table_bytes + 3) & ~3

    table, blob = bytearray(), bytearray()
    for name, payload, w, h in entries:
        # Every payload starts 4-byte aligned so image_decode_cri() can borrow
        # it in place instead of copying. This is the whole point of the format.
        pad = (-len(blob)) & 3
        blob += b"\0" * pad
        entry_off = offset + len(blob)
        table += name.encode("utf-8").ljust(CRA_NAME_MAX, b"\0")
        table += struct.pack("<IIHH", entry_off, len(payload), w, h)
        blob += payload

    total = offset + len(blob)
    header = CRA_MAGIC + struct.pack("<III", len(entries), total, 0)
    assert len(header) == CRA_HEADER_BYTES
    assert len(table) == len(entries) * CRA_ENTRY_BYTES
    return bytes(header + table + b"\0" * (offset - table_bytes) + blob)


# ── Driver ──────────────────────────────────────────────────────────────────
def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--source", action="append", default=[], type=Path,
                    help="icon theme checkout; repeatable, searched in order")
    ap.add_argument("--out", type=Path, default=Path("assets/careos-icons.cra"))
    ap.add_argument("--sizes", default=",".join(str(s) for s in DEFAULT_SIZES),
                    help="sizes to bake (must match ICON_SIZE_COUNT in gui/icon.h)")
    ap.add_argument("--wallpaper", type=Path,
                    help="image baked to wallpapers/default.cri")
    ap.add_argument("--wallpaper-size", default="1920x1080")
    ap.add_argument("--no-rle", action="store_true", help="store uncompressed")
    ap.add_argument("--emit-dir", type=Path,
                    help="also write loose .cri files here, for .care packaging")
    args = ap.parse_args()

    sizes = [int(s) for s in args.sizes.split(",") if s.strip()]
    if not args.source and not args.wallpaper:
        ap.error("nothing to do: pass --source and/or --wallpaper")

    for s in args.source:
        if not s.is_dir():
            return f"source is not a directory: {s}"

    print(f"renderer: {pick_renderer()}")
    index = build_index(args.source)
    print(f"indexed {len(index)} icon director{'y' if len(index) == 1 else 'ies'}")

    entries, missing = [], []
    for token, candidates in sorted(ICON_MAP.items()):
        resolved_any = False
        for size in sizes:
            path, src_size = find_icon(index, candidates, size)
            if path is None:
                continue
            try:
                img = rasterise(path, size)
            except Exception as exc:                      # noqa: BLE001
                print(f"  !! {token}@{size}: {exc}")
                continue
            payload = encode_cri(img, allow_rle=not args.no_rle)
            entries.append((f"icons/{size}/{token}.cri", payload, size, size))
            if args.emit_dir:
                out_dir = args.emit_dir / str(size)
                out_dir.mkdir(parents=True, exist_ok=True)
                (out_dir / f"{token}.cri").write_bytes(payload)
            resolved_any = True
        if resolved_any:
            print(f"  {token:<18} <- {Path(path).name}")
        else:
            missing.append(token)

    if args.wallpaper:
        w, h = (int(v) for v in args.wallpaper_size.lower().split("x"))
        img = Image.open(args.wallpaper).convert("RGBA")
        img = img.resize((w, h), Image.LANCZOS)
        payload = encode_cri(img, allow_rle=not args.no_rle)
        entries.append(("wallpapers/default.cri", payload, w, h))
        print(f"  wallpaper          <- {args.wallpaper.name} ({w}x{h})")

    if missing:
        print(f"\nno source for {len(missing)}: {', '.join(missing)}")
        print("these keep their vector glyph from gfx_draw_icon()")

    blob = build_cra(entries)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(blob)
    print(f"\nwrote {args.out}: {len(entries)} entries, {len(blob) / 1024:.1f} KiB")
    return 0


if __name__ == "__main__":
    sys.exit(main())
