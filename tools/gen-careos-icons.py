#!/usr/bin/env python3
"""Generate the bespoke CareOS icon theme using only Pillow.

Every icon is drawn procedurally at 256 px (for clean LANCZOS down-scaling)
and written to  tools/careos-theme/<size>x<size>/<category>/<name>.png
so gen-icons.py can ingest the directory as a --source.

Design language (Care Design Language icons):
  - App icons: rounded-squircle gradient container + white geometric symbol.
  - Folder/file icons: standalone shapes, no squircle wrapper.
  - 22 % corner radius on containers.
  - Consistent stroke weight, perspective, and palette.

Usage:
    source tools/.venv/bin/activate
    python3 tools/gen-careos-icons.py            # writes tools/careos-theme/
    python3 tools/gen-icons.py --source tools/careos-theme ...
"""

import math, os, sys
from pathlib import Path

try:
    from PIL import Image, ImageDraw
except ImportError:
    sys.exit("Pillow is required: pip install Pillow")

# ── Render constants ────────────────────────────────────────────────────────
SZ  = 256                        # master render size
PAD = 16                         # edge padding inside canvas
R   = 56                         # container corner radius (~22 %)
SW  = 6                          # base stroke width
SIZES = [16, 24, 32, 48, 64]    # sizes gen-icons.py bakes

# Symbol area: region inside the container where glyphs are drawn
SYM = 56                         # inset from edge to symbol area
CX, CY = SZ // 2, SZ // 2       # canvas center

# ── Color helpers ───────────────────────────────────────────────────────────
def hx(h):
    """'#RRGGBB' → (R, G, B, 255)."""
    h = h.lstrip('#')
    return (int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16), 255)

def lerp(c1, c2, t):
    return tuple(int(a * (1 - t) + b * t) for a, b in zip(c1, c2))

W   = (255, 255, 255, 255)
W80 = (255, 255, 255, 204)
W60 = (255, 255, 255, 153)
W40 = (255, 255, 255, 102)
W20 = (255, 255, 255, 51)

# ── Container builder ──────────────────────────────────────────────────────
def make_bg(top, bot):
    """Gradient-filled rounded squircle with top highlight."""
    img = Image.new('RGBA', (SZ, SZ), (0, 0, 0, 0))
    # Vertical gradient
    grad = Image.new('RGBA', (SZ, SZ))
    d = ImageDraw.Draw(grad)
    c1, c2 = hx(top), hx(bot)
    for y in range(SZ):
        d.line([(0, y), (SZ - 1, y)], fill=lerp(c1, c2, y / (SZ - 1)))
    # Clip to rounded rect
    mask = Image.new('L', (SZ, SZ), 0)
    ImageDraw.Draw(mask).rounded_rectangle(
        [PAD, PAD, SZ - PAD - 1, SZ - PAD - 1], radius=R, fill=255)
    img.paste(grad, mask=mask)
    # Top sheen (frosted glass)
    hl = Image.new('RGBA', (SZ, SZ), (0, 0, 0, 0))
    hm = Image.new('L', (SZ, SZ), 0)
    ImageDraw.Draw(hm).rounded_rectangle(
        [PAD + 3, PAD + 3, SZ - PAD - 4, PAD + (SZ - 2 * PAD) // 3],
        radius=max(R - 4, 0), fill=28)
    hl.paste(Image.new('RGBA', (SZ, SZ), W), mask=hm)
    return Image.alpha_composite(img, hl)


# ══════════════════════════════════════════════════════════════════════════════
#  APP ICONS — gradient squircle + white symbol
# ══════════════════════════════════════════════════════════════════════════════

def draw_terminal(img):
    d = ImageDraw.Draw(img)
    x0, y0 = SYM + 10, SYM + 20
    # ">" chevron
    pts = [(x0, y0), (x0 + 44, y0 + 36), (x0, y0 + 72)]
    d.line(pts, fill=W, width=SW + 2, joint='curve')
    # "_" cursor
    d.line([(x0 + 56, y0 + 72), (x0 + 110, y0 + 72)], fill=W, width=SW + 2)

def draw_notes(img):
    d = ImageDraw.Draw(img)
    # Notepad body
    x, y = SYM + 20, SYM - 4
    w, h = SZ - 2 * SYM - 40, SZ - 2 * SYM + 8
    d.rounded_rectangle([x, y, x + w, y + h], radius=12, fill=W40)
    d.rounded_rectangle([x, y, x + w, y + h], radius=12, outline=W80, width=3)
    # Spiral binding dots
    for i in range(5):
        cy = y + 28 + i * 24
        d.ellipse([x - 6, cy - 3, x + 6, cy + 3], fill=W80)
    # Lines
    for i in range(4):
        ly = y + 30 + i * 24
        d.line([(x + 24, ly), (x + w - 16, ly)], fill=W60, width=2)

def draw_files(img):
    d = ImageDraw.Draw(img)
    x, y = SYM + 4, SYM + 16
    w, h = SZ - 2 * SYM - 8, SZ - 2 * SYM - 16
    # Tab
    d.rounded_rectangle([x, y, x + w // 2, y + 20], radius=8, fill=W60)
    # Body
    d.rounded_rectangle([x, y + 14, x + w, y + h], radius=10, fill=W40)
    d.rounded_rectangle([x, y + 14, x + w, y + h], radius=10, outline=W80, width=3)

def draw_sysmon(img):
    d = ImageDraw.Draw(img)
    x0 = SYM + 12
    bot = SZ - SYM - 8
    bw = 22
    gap = 8
    heights = [40, 72, 56, 96, 64]
    for i, h in enumerate(heights):
        bx = x0 + i * (bw + gap)
        by = bot - h
        d.rounded_rectangle([bx, by, bx + bw, bot], radius=6, fill=W if i == 3 else W60)

def draw_calc(img):
    d = ImageDraw.Draw(img)
    x, y = SYM + 8, SYM + 4
    w, h = SZ - 2 * SYM - 16, SZ - 2 * SYM - 8
    # Display
    d.rounded_rectangle([x, y, x + w, y + 36], radius=8, fill=W40)
    d.rounded_rectangle([x, y, x + w, y + 36], radius=8, outline=W60, width=2)
    # Buttons grid (3x3)
    bs = 30
    gp = 8
    bx0 = x + (w - 3 * bs - 2 * gp) // 2
    by0 = y + 48
    for row in range(3):
        for col in range(3):
            px = bx0 + col * (bs + gp)
            py = by0 + row * (bs + gp)
            fill = W80 if (row == 2 and col == 2) else W40
            d.rounded_rectangle([px, py, px + bs, py + bs], radius=6, fill=fill)

def draw_about(img):
    d = ImageDraw.Draw(img)
    r = 48
    d.ellipse([CX - r, CY - r, CX + r, CY + r], outline=W, width=SW)
    # "i" letter: dot + stroke
    d.ellipse([CX - 4, CY - 30, CX + 4, CY - 22], fill=W)
    d.line([(CX, CY - 14), (CX, CY + 28)], fill=W, width=SW)

def draw_help(img):
    d = ImageDraw.Draw(img)
    r = 48
    d.ellipse([CX - r, CY - r, CX + r, CY + r], outline=W, width=SW)
    # "?" - arc + dot
    d.arc([CX - 20, CY - 36, CX + 20, CY], start=200, end=360, fill=W, width=SW)
    d.line([(CX, CY), (CX, CY + 12)], fill=W, width=SW)
    d.ellipse([CX - 4, CY + 22, CX + 4, CY + 30], fill=W)

def draw_browser(img):
    d = ImageDraw.Draw(img)
    r = 52
    # Globe
    d.ellipse([CX - r, CY - r, CX + r, CY + r], outline=W, width=SW - 1)
    # Vertical meridian
    d.ellipse([CX - r // 2, CY - r, CX + r // 2, CY + r], outline=W80, width=3)
    # Horizontal parallels
    d.line([(CX - r, CY), (CX + r, CY)], fill=W80, width=3)
    d.line([(CX - r + 10, CY - r // 2), (CX + r - 10, CY - r // 2)], fill=W60, width=2)
    d.line([(CX - r + 10, CY + r // 2), (CX + r - 10, CY + r // 2)], fill=W60, width=2)

def draw_settings(img):
    d = ImageDraw.Draw(img)
    # Gear: outer teeth + inner circle
    teeth = 8
    outer_r = 56
    inner_r = 40
    hole_r = 22
    for i in range(teeth):
        angle = i * 2 * math.pi / teeth
        a1 = angle - 0.25
        a2 = angle + 0.25
        pts = []
        for a in [a1, a1, a2, a2]:
            rr = outer_r if a in [a1, a2] else inner_r
            pts.append((CX + int(rr * math.cos(a)), CY + int(rr * math.sin(a))))
        # Draw tooth as thick line
        x1 = CX + int(outer_r * math.cos(angle))
        y1 = CY + int(outer_r * math.sin(angle))
        x2 = CX + int((inner_r + 4) * math.cos(angle))
        y2 = CY + int((inner_r + 4) * math.sin(angle))
        d.line([(x2, y2), (x1, y1)], fill=W, width=14)
    # Ring
    d.ellipse([CX - inner_r, CY - inner_r, CX + inner_r, CY + inner_r], fill=W)
    # Hole
    # Get the background color at center to punch through
    d.ellipse([CX - hole_r, CY - hole_r, CX + hole_r, CY + hole_r], fill=(0, 0, 0, 0))

def draw_settings_v2(img):
    """Gear icon using a cleaner approach."""
    d = ImageDraw.Draw(img)
    # We'll draw the gear as a series of shapes on a temporary layer
    gear = Image.new('RGBA', (SZ, SZ), (0, 0, 0, 0))
    gd = ImageDraw.Draw(gear)
    # Outer ring
    outer = 54
    gd.ellipse([CX - outer, CY - outer, CX + outer, CY + outer], fill=W)
    # Teeth
    teeth = 8
    for i in range(teeth):
        angle = i * 2 * math.pi / teeth
        tx = CX + int(outer * math.cos(angle))
        ty = CY + int(outer * math.sin(angle))
        gd.rounded_rectangle([tx - 10, ty - 10, tx + 10, ty + 10],
                             radius=4, fill=W)
    # Inner hole (transparent punch)
    hole = 20
    gd.ellipse([CX - hole, CY - hole, CX + hole, CY + hole], fill=(0, 0, 0, 0))
    img.paste(Image.alpha_composite(Image.new('RGBA', (SZ, SZ), (0,0,0,0)), gear), (0, 0), gear)

def draw_packages(img):
    d = ImageDraw.Draw(img)
    x, y = SYM + 8, SYM + 16
    w, h = SZ - 2 * SYM - 16, SZ - 2 * SYM - 24
    # Box body
    d.rounded_rectangle([x, y + 20, x + w, y + h], radius=8, fill=W40)
    d.rounded_rectangle([x, y + 20, x + w, y + h], radius=8, outline=W80, width=3)
    # Open flaps (two triangles)
    mid = x + w // 2
    d.polygon([(x + 4, y + 24), (mid, y), (mid, y + 24)], fill=W60)
    d.polygon([(x + w - 4, y + 24), (mid, y), (mid, y + 24)], fill=W40)
    # Center seam
    d.line([(mid, y + 24), (mid, y + h - 4)], fill=W60, width=2)
    # Down arrow
    aw = 16
    d.line([(mid, y + h // 2 - 10), (mid, y + h // 2 + 14)], fill=W, width=4)
    d.polygon([(mid - aw, y + h // 2 + 4), (mid + aw, y + h // 2 + 4),
               (mid, y + h // 2 + 24)], fill=W)

def draw_editor(img):
    d = ImageDraw.Draw(img)
    # Document
    dx, dy = SYM + 16, SYM
    dw, dh = 90, 130
    d.rounded_rectangle([dx, dy, dx + dw, dy + dh], radius=8, fill=W40)
    d.rounded_rectangle([dx, dy, dx + dw, dy + dh], radius=8, outline=W60, width=2)
    # Lines
    for i in range(4):
        lw = dw - 30 if i == 3 else dw - 24
        d.line([(dx + 12, dy + 24 + i * 22), (dx + 12 + lw, dy + 24 + i * 22)],
               fill=W60, width=2)
    # Pencil diagonal
    px, py = SYM + 80, SYM + 60
    pe = (SZ - SYM - 16, SZ - SYM - 8)
    d.line([(px, py), pe], fill=W, width=SW + 2)
    # Pencil tip
    d.polygon([(pe[0] - 8, pe[1] + 2), (pe[0] + 2, pe[1] - 8), (pe[0] + 6, pe[1] + 6)], fill=W80)

def draw_paint(img):
    d = ImageDraw.Draw(img)
    # Palette (large circle)
    pr = 56
    d.ellipse([CX - pr, CY - pr, CX + pr, CY + pr], fill=W40, outline=W60, width=3)
    # Thumb hole
    d.ellipse([CX - 40, CY + 10, CX - 18, CY + 32], fill=(0, 0, 0, 0))
    # Color dots (using accents from the CDL palette)
    dots = [
        (CX - 20, CY - 32, hx('#559AFF')),   # Blue
        (CX + 16, CY - 28, hx('#34C759')),    # Green
        (CX + 32, CY,      hx('#FF9F0A')),    # Orange
        (CX + 10, CY + 24, hx('#A855F7')),    # Purple
        (CX - 30, CY - 4,  hx('#FF5F57')),    # Red
    ]
    for dx, dy, c in dots:
        d.ellipse([dx - 10, dy - 10, dx + 10, dy + 10], fill=c)

def draw_clock(img):
    d = ImageDraw.Draw(img)
    r = 52
    # Face
    d.ellipse([CX - r, CY - r, CX + r, CY + r], outline=W, width=SW)
    # Hour marks
    for i in range(12):
        angle = i * math.pi / 6 - math.pi / 2
        x1 = CX + int((r - 10) * math.cos(angle))
        y1 = CY + int((r - 10) * math.sin(angle))
        x2 = CX + int((r - 3) * math.cos(angle))
        y2 = CY + int((r - 3) * math.sin(angle))
        d.line([(x1, y1), (x2, y2)], fill=W60, width=2)
    # Hour hand (10:10 position is classic)
    d.line([(CX, CY), (CX - 18, CY - 28)], fill=W, width=SW)
    # Minute hand
    d.line([(CX, CY), (CX + 28, CY - 18)], fill=W, width=4)
    # Center dot
    d.ellipse([CX - 5, CY - 5, CX + 5, CY + 5], fill=W)

def draw_netmon(img):
    d = ImageDraw.Draw(img)
    # Signal bars (Wi-Fi arcs style)
    base_x, base_y = CX, SZ - SYM - 8
    for i in range(4):
        r_inner = 16 + i * 20
        r_outer = r_inner + 10
        bbox = [base_x - r_outer, base_y - r_outer, base_x + r_outer, base_y + r_outer]
        alpha_fill = W if i < 3 else W60
        d.arc(bbox, start=225, end=315, fill=alpha_fill, width=SW)
    # Center dot
    d.ellipse([base_x - 6, base_y - 6, base_x + 6, base_y + 6], fill=W)

def draw_users(img):
    d = ImageDraw.Draw(img)
    # Head
    hr = 24
    d.ellipse([CX - hr, CY - 44 - hr, CX + hr, CY - 44 + hr],
              fill=W40, outline=W, width=SW)
    # Body (shoulders arc)
    body_r = 48
    d.arc([CX - body_r, CY + 4, CX + body_r, CY + 4 + body_r * 2],
          start=180, end=360, fill=W, width=SW)
    # Fill the body
    d.chord([CX - body_r, CY + 4, CX + body_r, CY + 4 + body_r * 2],
            start=180, end=360, fill=W40)

def draw_maze(img):
    d = ImageDraw.Draw(img)
    # Simple maze grid pattern
    x0, y0 = SYM + 8, SYM + 8
    sz = SZ - 2 * SYM - 16
    cell = sz // 5
    # Outer border
    d.rounded_rectangle([x0, y0, x0 + sz, y0 + sz], radius=8, outline=W, width=SW)
    # Internal walls (simplified maze)
    walls = [
        ((1, 0), (1, 2)), ((2, 1), (4, 1)), ((0, 2), (2, 2)),
        ((3, 2), (3, 4)), ((1, 3), (2, 3)), ((4, 3), (4, 4)),
        ((0, 4), (2, 4)),
    ]
    for (c1, r1), (c2, r2) in walls:
        d.line([(x0 + c1 * cell, y0 + r1 * cell),
                (x0 + c2 * cell, y0 + r2 * cell)], fill=W60, width=3)
    # Entry/exit markers
    d.ellipse([x0 + 4, y0 + 4, x0 + 16, y0 + 16], fill=W)
    d.ellipse([x0 + sz - 16, y0 + sz - 16, x0 + sz - 4, y0 + sz - 4], fill=W80)

def draw_3d(img):
    d = ImageDraw.Draw(img)
    # Isometric cube
    w = 50
    h = 30
    # Front face
    pts_front = [(CX - w, CY), (CX, CY + h), (CX + w, CY), (CX, CY - h)]
    d.polygon(pts_front, fill=W40, outline=W, width=3)
    # Top face
    pts_top = [(CX, CY - h), (CX - w, CY), (CX, CY - h - h), (CX + w, CY - h - h + h)]
    pts_top_actual = [(CX - w, CY), (CX, CY - h * 2), (CX + w, CY), (CX, CY - h)]
    # Simplified: draw the 3 visible faces of a cube
    # Bottom-left face
    d.polygon([(CX - w, CY), (CX, CY + h), (CX, CY - h + h), (CX - w, CY - h + h - h)],
              fill=W40)
    # Bottom-right face
    d.polygon([(CX + w, CY), (CX, CY + h), (CX, CY), (CX + w, CY)], fill=W20)
    # Top face
    d.polygon([(CX - w, CY), (CX, CY - h), (CX + w, CY), (CX, CY + h)], fill=W60)
    # Edges
    d.line([(CX - w, CY), (CX, CY - h)], fill=W, width=3)
    d.line([(CX, CY - h), (CX + w, CY)], fill=W, width=3)
    d.line([(CX + w, CY), (CX, CY + h)], fill=W, width=3)
    d.line([(CX, CY + h), (CX - w, CY)], fill=W, width=3)
    # Vertical back edge
    d.line([(CX, CY - h), (CX, CY + h)], fill=W80, width=2)

def draw_doom(img):
    d = ImageDraw.Draw(img)
    # Flame / fire icon
    flames = [
        # (cx_offset, base_y, tip_y, width)
        (0, 50, -50, 40),
        (-30, 50, -30, 28),
        (28, 50, -20, 24),
        (-14, 50, -42, 32),
        (14, 50, -36, 28),
    ]
    for ox, by, ty, fw in flames:
        bx = CX + ox
        base = CY + by
        tip = CY + ty
        d.polygon([(bx - fw // 2, base), (bx, tip), (bx + fw // 2, base)], fill=W40)
    # Brighter core
    d.polygon([(CX - 16, CY + 50), (CX, CY - 34), (CX + 16, CY + 50)], fill=W60)
    d.polygon([(CX - 8, CY + 50), (CX, CY - 16), (CX + 8, CY + 50)], fill=W)

def draw_generic(img):
    d = ImageDraw.Draw(img)
    # Window outline
    x, y = SYM + 8, SYM + 4
    w, h = SZ - 2 * SYM - 16, SZ - 2 * SYM - 8
    d.rounded_rectangle([x, y, x + w, y + h], radius=10, outline=W, width=SW)
    # Title bar
    d.line([(x, y + 28), (x + w, y + 28)], fill=W60, width=2)
    # Window control dots
    d.ellipse([x + 10, y + 10, x + 18, y + 18], fill=hx('#FF5F57'))
    d.ellipse([x + 24, y + 10, x + 32, y + 18], fill=hx('#FFBD2E'))
    d.ellipse([x + 38, y + 10, x + 46, y + 18], fill=hx('#28C840'))


# ══════════════════════════════════════════════════════════════════════════════
#  FOLDER ICONS — standalone shapes, not inside a squircle
# ══════════════════════════════════════════════════════════════════════════════

def make_folder(accent_top='#42A5F5', accent_bot='#1E88E5'):
    """Base folder shape with gradient."""
    img = Image.new('RGBA', (SZ, SZ), (0, 0, 0, 0))
    c1, c2 = hx(accent_top), hx(accent_bot)
    # Gradient layer
    grad = Image.new('RGBA', (SZ, SZ))
    gd = ImageDraw.Draw(grad)
    for y in range(SZ):
        gd.line([(0, y), (SZ - 1, y)], fill=lerp(c1, c2, y / (SZ - 1)))

    mask = Image.new('L', (SZ, SZ), 0)
    md = ImageDraw.Draw(mask)
    x, y = 24, 50
    w, h = SZ - 48, SZ - 74
    # Tab
    md.rounded_rectangle([x, y, x + w // 2 + 10, y + 30], radius=12, fill=255)
    # Body
    md.rounded_rectangle([x, y + 16, x + w, y + h], radius=14, fill=255)

    img.paste(grad, mask=mask)
    # Lighter front flap
    flap = Image.new('RGBA', (SZ, SZ), (0, 0, 0, 0))
    flap_mask = Image.new('L', (SZ, SZ), 0)
    fmd = ImageDraw.Draw(flap_mask)
    fmd.rounded_rectangle([x, y + 36, x + w, y + h], radius=12, fill=60)
    flap.paste(Image.new('RGBA', (SZ, SZ), W), mask=flap_mask)
    img = Image.alpha_composite(img, flap)
    return img

def add_emblem(img, draw_fn):
    """Draw an emblem on a folder icon."""
    draw_fn(ImageDraw.Draw(img))
    return img

def emblem_documents(d):
    cx, cy = CX + 10, CY + 20
    d.rounded_rectangle([cx - 16, cy - 22, cx + 16, cy + 22], radius=4, fill=W60)
    for i in range(3):
        d.line([(cx - 10, cy - 12 + i * 12), (cx + 10, cy - 12 + i * 12)], fill=W, width=2)

def emblem_download(d):
    cx, cy = CX + 10, CY + 20
    d.line([(cx, cy - 20), (cx, cy + 10)], fill=W, width=SW)
    d.polygon([(cx - 14, cy + 4), (cx, cy + 22), (cx + 14, cy + 4)], fill=W)
    d.line([(cx - 18, cy + 24), (cx + 18, cy + 24)], fill=W, width=3)

def emblem_music(d):
    cx, cy = CX + 10, CY + 16
    d.ellipse([cx - 10, cy + 6, cx + 4, cy + 20], fill=W)
    d.line([(cx + 2, cy + 12), (cx + 2, cy - 24)], fill=W, width=3)
    d.line([(cx + 2, cy - 24), (cx + 20, cy - 28)], fill=W, width=3)

def emblem_pictures(d):
    cx, cy = CX + 10, CY + 18
    # Mountain
    d.polygon([(cx - 18, cy + 16), (cx - 4, cy - 10), (cx + 8, cy + 16)], fill=W60)
    d.polygon([(cx, cy + 16), (cx + 10, cy), (cx + 20, cy + 16)], fill=W)
    # Sun
    d.ellipse([cx + 6, cy - 18, cx + 18, cy - 6], fill=W)

def emblem_videos(d):
    cx, cy = CX + 10, CY + 18
    d.polygon([(cx - 12, cy - 16), (cx - 12, cy + 16), (cx + 16, cy)], fill=W)

def emblem_home(d):
    cx, cy = CX + 10, CY + 18
    # Roof triangle
    d.polygon([(cx - 22, cy), (cx, cy - 22), (cx + 22, cy)], fill=W)
    # Walls
    d.rectangle([cx - 16, cy, cx + 16, cy + 18], fill=W80)
    # Door
    d.rectangle([cx - 6, cy + 4, cx + 6, cy + 18], fill=(0, 0, 0, 0))


# ══════════════════════════════════════════════════════════════════════════════
#  FILE / DEVICE ICONS — standalone document/device shapes
# ══════════════════════════════════════════════════════════════════════════════

def make_document(corner_fold=True):
    """White document shape."""
    img = Image.new('RGBA', (SZ, SZ), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    x, y = 48, 20
    w, h = SZ - 96, SZ - 40
    fold = 32
    if corner_fold:
        pts = [(x, y + 12), (x + 12, y), (x + w - fold, y), (x + w, y + fold),
               (x + w, y + h - 12), (x + w - 12, y + h), (x + 12, y + h), (x, y + h - 12)]
        d.polygon(pts, fill=(240, 244, 248, 240))
        d.polygon(pts, outline=(200, 210, 220, 255))
        # Fold triangle
        d.polygon([(x + w - fold, y), (x + w, y + fold), (x + w - fold, y + fold)],
                  fill=(220, 228, 236, 200))
        d.line([(x + w - fold, y), (x + w - fold, y + fold), (x + w, y + fold)],
               fill=(200, 210, 220, 255), width=1)
    else:
        d.rounded_rectangle([x, y, x + w, y + h], radius=10,
                           fill=(240, 244, 248, 240), outline=(200, 210, 220, 255), width=2)
    return img, d, (x, y, w, h)

def draw_text_file():
    img, d, (x, y, w, h) = make_document()
    for i in range(6):
        lw = w - 60 if i == 5 else w - 44
        d.line([(x + 22, y + 50 + i * 24), (x + 22 + lw, y + 50 + i * 24)],
               fill=(160, 175, 195, 200), width=2)
    return img

def draw_executable():
    img, d, (x, y, w, h) = make_document()
    # Lines
    for i in range(3):
        d.line([(x + 22, y + 48 + i * 22), (x + w - 22, y + 48 + i * 22)],
               fill=(160, 175, 195, 150), width=2)
    # Gear emblem
    cx, cy = x + w // 2, y + h - 56
    gr = 24
    d.ellipse([cx - gr, cy - gr, cx + gr, cy + gr], fill=(100, 120, 160, 200))
    d.ellipse([cx - 10, cy - 10, cx + 10, cy + 10], fill=(240, 244, 248, 240))
    # Teeth
    for i in range(6):
        angle = i * math.pi / 3
        tx = cx + int(gr * math.cos(angle))
        ty = cy + int(gr * math.sin(angle))
        d.ellipse([tx - 6, ty - 6, tx + 6, ty + 6], fill=(100, 120, 160, 200))
    return img

def draw_image_file():
    img, d, (x, y, w, h) = make_document(corner_fold=False)
    # Landscape: mountain + sun
    mx, my = x + w // 2, y + h // 2 + 20
    # Sun
    d.ellipse([x + w - 60, y + 40, x + w - 28, y + 72], fill=(255, 200, 60, 230))
    # Mountains
    d.polygon([(x + 10, my + 40), (x + w // 3, my - 20), (x + w // 2, my + 40)],
              fill=(100, 180, 120, 200))
    d.polygon([(x + w // 3, my + 40), (x + 2 * w // 3, my - 40), (x + w - 10, my + 40)],
              fill=(80, 160, 100, 220))
    return img

def draw_drive():
    img = Image.new('RGBA', (SZ, SZ), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    x, y = 40, 48
    w, h = SZ - 80, SZ - 96
    # Body
    d.rounded_rectangle([x, y + 20, x + w, y + h], radius=14,
                       fill=(140, 150, 170, 230), outline=(120, 130, 150, 255), width=3)
    # Top ellipse (3D effect)
    d.ellipse([x, y, x + w, y + 44], fill=(170, 180, 200, 240),
             outline=(120, 130, 150, 255), width=3)
    # Activity LED
    d.ellipse([x + w - 28, y + h - 6, x + w - 16, y + h + 6], fill=(80, 200, 120, 230))
    return img


# ══════════════════════════════════════════════════════════════════════════════
#  ICON REGISTRY — maps upstream filename → (category, builder)
# ══════════════════════════════════════════════════════════════════════════════

# App icons: (upstream_name, category, top_gradient, bottom_gradient, symbol_fn)
APP_ICONS = [
    ("utilities-terminal",          "apps", "#2D3436", "#636E72", draw_terminal),
    ("accessories-notes",           "apps", "#FFC107", "#FF9800", draw_notes),
    ("system-file-manager",         "apps", "#42A5F5", "#1E88E5", draw_files),
    ("utilities-system-monitor",    "apps", "#66BB6A", "#388E3C", draw_sysmon),
    ("accessories-calculator",      "apps", "#EF5350", "#C62828", draw_calc),
    ("help-about",                  "apps", "#AB47BC", "#7B1FA2", draw_about),
    ("help-contents",               "apps", "#5C6BC0", "#303F9F", draw_help),
    ("web-browser",                 "apps", "#29B6F6", "#0277BD", draw_browser),
    ("preferences-system",          "apps", "#78909C", "#455A64", draw_settings_v2),
    ("system-software-install",     "apps", "#FF7043", "#D84315", draw_packages),
    ("accessories-text-editor",     "apps", "#26A69A", "#00695C", draw_editor),
    ("kolourpaint",                 "apps", "#EC407A", "#AD1457", draw_paint),
    ("accessories-clock",           "apps", "#26C6DA", "#00838F", draw_clock),
    ("preferences-system-network",  "apps", "#9CCC65", "#558B2F", draw_netmon),
    ("system-users",                "apps", "#7E57C2", "#4527A0", draw_users),
    ("gnome-maze",                  "apps", "#FFA726", "#E65100", draw_maze),
    ("applications-graphics",       "apps", "#FF7043", "#BF360C", draw_3d),
    ("gnome-boxes",                 "apps", "#B71C1C", "#D32F2F", draw_doom),
    ("application-default-icon",    "apps", "#90A4AE", "#546E7A", draw_generic),
]

# Folder icons: (upstream_name, category, emblem_fn_or_None)
FOLDER_ICONS = [
    ("folder",            "places", None),
    ("folder-documents",  "places", emblem_documents),
    ("folder-download",   "places", emblem_download),
    ("folder-music",      "places", emblem_music),
    ("folder-pictures",   "places", emblem_pictures),
    ("folder-videos",     "places", emblem_videos),
    ("user-home",         "places", emblem_home),
]

# File / device icons: (upstream_name, category, builder_fn)
FILE_ICONS = [
    ("text-x-generic",          "mimetypes", draw_text_file),
    ("application-x-executable", "mimetypes", draw_executable),
    ("image-x-generic",          "mimetypes", draw_image_file),
    ("drive-harddisk",           "devices",   draw_drive),
]


# ══════════════════════════════════════════════════════════════════════════════
#  GENERATION
# ══════════════════════════════════════════════════════════════════════════════

def generate_all(out_dir: Path):
    total = 0

    # --- App icons (gradient container + symbol) ---
    for name, cat, top, bot, sym_fn in APP_ICONS:
        img = make_bg(top, bot)
        sym_fn(img)
        for sz in SIZES:
            dest = out_dir / f"{sz}x{sz}" / cat
            dest.mkdir(parents=True, exist_ok=True)
            resized = img.resize((sz, sz), Image.LANCZOS)
            resized.save(dest / f"{name}.png")
        total += 1
        print(f"  {name:<34} <- app icon")

    # --- Folder icons ---
    for name, cat, emblem_fn in FOLDER_ICONS:
        img = make_folder()
        if emblem_fn:
            add_emblem(img, emblem_fn)
        for sz in SIZES:
            dest = out_dir / f"{sz}x{sz}" / cat
            dest.mkdir(parents=True, exist_ok=True)
            resized = img.resize((sz, sz), Image.LANCZOS)
            resized.save(dest / f"{name}.png")
        total += 1
        print(f"  {name:<34} <- folder icon")

    # --- File / device icons ---
    for name, cat, builder_fn in FILE_ICONS:
        img = builder_fn()
        for sz in SIZES:
            dest = out_dir / f"{sz}x{sz}" / cat
            dest.mkdir(parents=True, exist_ok=True)
            resized = img.resize((sz, sz), Image.LANCZOS)
            resized.save(dest / f"{name}.png")
        total += 1
        print(f"  {name:<34} <- file/device icon")

    print(f"\n  {total} icons x {len(SIZES)} sizes = {total * len(SIZES)} PNGs")


def main():
    out = Path(__file__).resolve().parent / "careos-theme"
    print(f"CareOS bespoke icon generator")
    print(f"  render size : {SZ}px")
    print(f"  output sizes: {SIZES}")
    print(f"  output dir  : {out}\n")
    generate_all(out)
    print(f"\nDone. Run gen-icons.py with --source {out.name} to bake the archive.")
    return 0

if __name__ == "__main__":
    sys.exit(main())
