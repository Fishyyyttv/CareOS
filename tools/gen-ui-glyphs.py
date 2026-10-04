#!/usr/bin/env python3
"""Flatten the v10 system glyphs into polyline tables for gui/ui_glyph_raster.c.

The glyphs are the 24-unit, 2-unit-stroke icons of the CareOS Design canvas
(docs/design/careos-v10/*.dc.html), written here as SVG path data. Arcs and
curves are flattened to polylines; the kernel rasterises them with round-cap
anti-aliased strokes at whatever size is asked for. Stdlib only.

  python3 tools/gen-ui-glyphs.py > gui/ui_glyph_data.h

The order below MUST match ui_glyph_t in gui/ui.h.
"""
import math
import re
import sys

# (enum suffix, svg path, fill). fill=True rasterises the closed outline as a
# solid shape instead of stroking it.
GLYPHS = [
    ("WIFI", "M2.5 9a14 14 0 0 1 19 0M5.8 12.5a9.5 9.5 0 0 1 12.4 0M9.2 16a4.6 4.6 0 0 1 5.6 0M12 19.5h.01", False),
    ("WIRED", "M9 3.5h6v5H9zM12 8.5v3.5M5 12h14M5 12v3.5M19 12v3.5M2.5 15.5h5v5h-5zM16.5 15.5h5v5h-5z", False),
    ("BLUETOOTH", "M7 7l10 10-5 5V2l5 5L7 17", False),
    ("SOUND", "M4 9.5h3.5L12 5.5v13l-4.5-4H4zM16 9a4.5 4.5 0 0 1 0 6M18.8 6.5a8 8 0 0 1 0 11", False),
    ("MUTE", "M4 9.5h3.5L12 5.5v13l-4.5-4H4zM16 9.5l5 5M21 9.5l-5 5", False),
    ("BATTERY", "M3 8h15v8H3zM20.5 10.5v3", False),
    ("SEARCH", "M11 4a7 7 0 1 0 0 14a7 7 0 1 0 0-14M20 20l-4-4", False),
    ("BELL", "M6 16.5V11a6 6 0 0 1 12 0v5.5l1.5 1.5h-15zM10 20.5a2 2 0 0 0 4 0", False),
    ("MOON", "M20 14.5A8 8 0 0 1 9.5 4a8 8 0 1 0 10.5 10.5z", False),
    ("SUN", "M12 8a4 4 0 1 0 0 8a4 4 0 1 0 0-8M12 2.5v2M12 19.5v2M2.5 12h2M19.5 12h2M5.3 5.3l1.4 1.4M17.3 17.3l1.4 1.4M5.3 18.7l1.4-1.4M17.3 6.7l1.4-1.4", False),
    ("LOCK", "M6 11h12v9H6zM8.5 11V8a3.5 3.5 0 0 1 7 0v3", False),
    ("POWER", "M12 3v8M6.5 6.5a8 8 0 1 0 11 0", False),
    ("RESTART", "M20 11a8 8 0 1 0-2.3 5.7M20 5v6h-6", False),
    ("USER", "M12 4a4 4 0 1 0 0 8a4 4 0 1 0 0-8M4.5 20.5a7.5 7.5 0 0 1 15 0", False),
    ("CLOCK", "M12 3.5a8.5 8.5 0 1 0 0 17a8.5 8.5 0 1 0 0-17M12 7.5V12l3 2", False),
    ("PLUS", "M12 5v14M5 12h14", False),
    ("SLIDERS", "M4 8h9M17 8h3M4 16h3M11 16h9M15 6v4M9 14v4", False),
    ("GRID", "M4 4h7v7H4zM13 4h7v7h-7zM4 13h7v7H4zM13 13h7v7h-7z", False),
    ("CHEVRON_L", "M15 6l-6 6 6 6", False),
    ("CHEVRON_R", "M9 6l6 6-6 6", False),
    ("CHEVRON_D", "M6 9l6 6 6-6", False),
    ("CLOSE", "M6 6l12 12M18 6L6 18", False),
    ("CHECK", "M5 12.5l4.5 4.5L19 7.5", False),
    ("ARROW_R", "M5 12h14M13 6l6 6-6 6", False),
    ("EYE", "M2.5 12S6 5.5 12 5.5 21.5 12 21.5 12 18 18.5 12 18.5 2.5 12 2.5 12zM12 9.5a2.5 2.5 0 1 0 0 5a2.5 2.5 0 1 0 0-5", False),
    ("CAMERA", "M4 8h3l2-2.5h6L17 8h3v11H4zM12 10.5a3.2 3.2 0 1 0 0 6.4a3.2 3.2 0 1 0 0-6.4", False),
    ("HOME", "M4 11l8-7 8 7v9h-5v-6H9v6H4z", False),
    ("FOLDER", "M3.5 6.5a1 1 0 0 1 1-1h5l2 2.5h8a1 1 0 0 1 1 1v9a1 1 0 0 1-1 1h-15a1 1 0 0 1-1-1z", False),
    ("TRASH", "M4 7h16M9.5 7V4.5h5V7M6 7l1 13h10l1-13M10 11v5.5M14 11v5.5", False),
    ("HEART", "M12 20s-7-4.4-7-10a4 4 0 0 1 7-2.6A4 4 0 0 1 19 10c0 5.6-7 10-7 10z", True),
]

# App icons (AppIcon artboard): (enum suffix, gradient top, gradient bottom,
# glyph colour, glyph path). Order MUST match ui_icon_t in gui/ui.h.
ICONS = [
    ("CARE", 0xFF9A76, 0xFF6A47, 0x2A120A, "M12 20s-7-4.4-7-10a4 4 0 0 1 7-2.6A4 4 0 0 1 19 10c0 5.6-7 10-7 10z"),
    ("TERMINAL", 0x4A5378, 0x2A3050, 0x7CF0B5, "M6 8l4 4-4 4M13 16h5"),
    ("FILES", 0x6E9BFF, 0x3F6DF2, 0xFFFFFF, "M3 7.5A2 2 0 0 1 5 5.5h4l2 2h8a2 2 0 0 1 2 2v7.5a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2zM3 10.5h18"),
    ("BROWSER", 0x3CC8E8, 0x1290C2, 0xFFFFFF, "M12 3a9 9 0 1 0 0 18a9 9 0 1 0 0-18M3 12h18M12 3c3.2 3.2 3.2 14.8 0 18M12 3c-3.2 3.2-3.2 14.8 0 18"),
    ("NOTES", 0xFFD36B, 0xF5AE2E, 0x3A2600, "M7 3.5h7l4 4V20a.5.5 0 0 1-.5.5h-10A.5.5 0 0 1 6 20V4.5a1 1 0 0 1 1-1zM14 3.5v4h4M9 12h6M9 16h4"),
    ("EDITOR", 0x9A86FF, 0x6A4FF0, 0xFFFFFF, "M8.5 8l-4 4 4 4M15.5 8l4 4-4 4M13.5 5l-3 14"),
    ("PAINT", 0xFF8FA8, 0xF25A80, 0xFFFFFF, "M12 3a9 9 0 1 0 0 18c1.4 0 2-1 2-2s-1-2 0-3 2-1 3-1h1a3 3 0 0 0 3-3c0-5-4-9-9-9zM7.5 11.5h.01M10 7.5h.01M15 7.5h.01"),
    ("MONITOR", 0x4FD68E, 0x1FA562, 0xFFFFFF, "M3 12.5h4l2.5-6 4 12 2.5-6h5"),
    ("CLOCK", 0xFFA766, 0xF2782E, 0xFFFFFF, "M12 3.5a8.5 8.5 0 1 0 0 17a8.5 8.5 0 1 0 0-17M12 7.5v4.5l3 2"),
    ("CALC", 0x7C86A3, 0x4E5775, 0xFFFFFF, "M7 3.5h10a1.5 1.5 0 0 1 1.5 1.5v14a1.5 1.5 0 0 1-1.5 1.5H7A1.5 1.5 0 0 1 5.5 19V5A1.5 1.5 0 0 1 7 3.5zM8.5 7.5h7M8.5 12h.01M12 12h.01M15.5 12h.01M8.5 16h.01M12 16h.01M15.5 16h.01"),
    ("NETMON", 0x36D1C0, 0x0E9E8F, 0xFFFFFF, "M4 19.5h16M7 15.5v-3M11 15.5V8M15 15.5v-5M19 15.5V5"),
    ("STORE", 0xC08BFF, 0x8E4DF0, 0xFFFFFF, "M12 3l8 4.5v9L12 21l-8-4.5v-9zM4 7.5l8 4.5 8-4.5M12 12v9"),
    ("SETTINGS", 0x8F9BB8, 0x5D6987, 0xFFFFFF, "M4 7h9M17 7h3M4 12h3M11 12h9M4 17h11M19 17h1M15 5v4M9 10v4M17 15v4"),
    ("USERS", 0x7E8CFF, 0x4F5EF2, 0xFFFFFF, "M9 11a3.5 3.5 0 1 0 0-7a3.5 3.5 0 1 0 0 7M3 20c0-3.5 2.7-5.5 6-5.5s6 2 6 5.5M16 4.3a3.5 3.5 0 0 1 0 6.4M21 20c0-2.8-1.6-4.6-3.6-5.2"),
    ("ABOUT", 0xB4A4E6, 0x7F6CC2, 0xFFFFFF, "M12 3.5a8.5 8.5 0 1 0 0 17a8.5 8.5 0 1 0 0-17M12 11v5.5M12 7.8h.01"),
    ("GAMES", 0xFF7E82, 0xEB4950, 0xFFFFFF, "M7 8h10a4.5 4.5 0 0 1 4.5 4.5v.5a3.5 3.5 0 0 1-6.2 2.2L14.5 14h-5l-.8 1.2A3.5 3.5 0 0 1 2.5 13v-.5A4.5 4.5 0 0 1 7 8zM7.5 10.5v3M6 12h3M15.5 11.5h.01M17.5 13.5h.01"),
    ("PHOTOS", 0xFF8EC4, 0xE2508F, 0xFFFFFF, "M5 4.5h14a1 1 0 0 1 1 1v13a1 1 0 0 1-1 1H5a1 1 0 0 1-1-1v-13a1 1 0 0 1 1-1zM4 16l5-5 4 4 2-2 5 5M15.5 9h.01"),
    ("MUSIC", 0xFF7AA0, 0xE9446B, 0xFFFFFF, "M9 18.5V5.5l11-2v13M9 18.5a2.5 2.5 0 1 1-5 0a2.5 2.5 0 1 1 5 0M20 16.5a2.5 2.5 0 1 1-5 0a2.5 2.5 0 1 1 5 0"),
    ("MAIL", 0x6CB8FF, 0x2F86EB, 0xFFFFFF, "M4 6h16v12H4zM4 7l8 6 8-6"),
    ("CALENDAR", 0xFFFFFF, 0xE9ECF5, 0xE5532E, "M5 5.5h14a1 1 0 0 1 1 1V19a1 1 0 0 1-1 1H5a1 1 0 0 1-1-1V6.5a1 1 0 0 1 1-1zM4 10h16M8 3.5v4M16 3.5v4M8.5 14h.01M12 14h.01M15.5 14h.01M8.5 17h.01M12 17h.01"),
    ("PET", 0xB9B0FF, 0x7E6CF2, 0xFFFFFF, "M12 6.5c4.2 0 7 3 7 7s-3 6-7 6-7-2-7-6 2.8-7 7-7zM12 6.5V4M9.5 12.5v1.5M14.5 12.5v1.5M10.5 16.2q1.5 1 3 0"),
    ("LAUNCHER", 0x3B4466, 0x262D4A, 0xFFFFFF, "M5 5h5v5H5zM14 5h5v5h-5zM5 14h5v5H5zM14 14h5v5h-5z"),
    ("TRASH", 0x5B6585, 0x3A4262, 0xE6E9F5, "M4.5 7h15M9.5 7V4.5h5V7M6.5 7l1 13h9l1-13M10 11v5M14 11v5"),
]

SCALE = 16  # Q4 viewbox units


def tokenize(d):
    for m in re.finditer(r"[MmLlHhVvAaCcSsQqZz]|-?(?:\d+\.?\d*|\.\d+)(?:e-?\d+)?", d):
        yield m.group(0)


def arc_points(p0, rx, ry, phi, large, sweep, p1):
    """SVG elliptical arc (endpoint form) -> list of points after p0."""
    x1, y1 = p0
    x2, y2 = p1
    if rx == 0 or ry == 0:
        return [p1]
    cp, sp = math.cos(math.radians(phi)), math.sin(math.radians(phi))
    dx, dy = (x1 - x2) / 2, (y1 - y2) / 2
    x1p, y1p = cp * dx + sp * dy, -sp * dx + cp * dy
    rx, ry = abs(rx), abs(ry)
    lam = x1p * x1p / (rx * rx) + y1p * y1p / (ry * ry)
    if lam > 1:
        rx, ry = rx * math.sqrt(lam), ry * math.sqrt(lam)
    num = rx * rx * ry * ry - rx * rx * y1p * y1p - ry * ry * x1p * x1p
    den = rx * rx * y1p * y1p + ry * ry * x1p * x1p
    co = math.sqrt(max(0.0, num / den)) if den else 0.0
    if large == sweep:
        co = -co
    cxp, cyp = co * rx * y1p / ry, -co * ry * x1p / rx
    cx = cp * cxp - sp * cyp + (x1 + x2) / 2
    cy = sp * cxp + cp * cyp + (y1 + y2) / 2

    def ang(ux, uy, vx, vy):
        a = math.atan2(ux * vy - uy * vx, ux * vx + uy * vy)
        return a

    t1 = ang(1, 0, (x1p - cxp) / rx, (y1p - cyp) / ry)
    dt = ang((x1p - cxp) / rx, (y1p - cyp) / ry, (-x1p - cxp) / rx, (-y1p - cyp) / ry)
    if not sweep and dt > 0:
        dt -= 2 * math.pi
    elif sweep and dt < 0:
        dt += 2 * math.pi
    n = max(2, int(abs(dt) / math.radians(9)) + 1)
    pts = []
    for i in range(1, n + 1):
        t = t1 + dt * i / n
        x = cp * rx * math.cos(t) - sp * ry * math.sin(t) + cx
        y = sp * rx * math.cos(t) + cp * ry * math.sin(t) + cy
        pts.append((x, y))
    pts[-1] = p1
    return pts


def cubic_points(p0, c1, c2, p1, n=12):
    pts = []
    for i in range(1, n + 1):
        t = i / n
        mt = 1 - t
        x = mt**3 * p0[0] + 3 * mt * mt * t * c1[0] + 3 * mt * t * t * c2[0] + t**3 * p1[0]
        y = mt**3 * p0[1] + 3 * mt * mt * t * c1[1] + 3 * mt * t * t * c2[1] + t**3 * p1[1]
        pts.append((x, y))
    return pts


def parse(d):
    """Return [(points, closed)] subpaths in viewbox units."""
    toks = list(tokenize(d))
    i = 0
    subs = []
    cur = None
    pos = (0.0, 0.0)
    start = pos
    last_c2 = None
    cmd = None

    def num():
        nonlocal i
        v = float(toks[i])
        i += 1
        return v

    while i < len(toks):
        if re.match(r"[A-Za-z]", toks[i]):
            cmd = toks[i]
            i += 1
        rel = cmd.islower()
        C = cmd.upper()
        ox, oy = pos if rel else (0.0, 0.0)
        if C == "M":
            pos = (ox + num(), oy + num())
            if cur:
                subs.append((cur, False))
            cur = [pos]
            start = pos
            cmd = "l" if rel else "L"
            last_c2 = None
        elif C == "L":
            pos = (ox + num(), oy + num()); cur.append(pos); last_c2 = None
        elif C == "H":
            pos = ((pos[0] if rel else 0) + num(), pos[1]); cur.append(pos); last_c2 = None
        elif C == "V":
            pos = (pos[0], (pos[1] if rel else 0) + num()); cur.append(pos); last_c2 = None
        elif C == "A":
            rx, ry, phi, large, sweep = num(), num(), num(), num(), num()
            end = (ox + num(), oy + num())
            cur.extend(arc_points(pos, rx, ry, phi, int(large), int(sweep), end))
            pos = end; last_c2 = None
        elif C == "C":
            c1 = (ox + num(), oy + num()); c2 = (ox + num(), oy + num()); end = (ox + num(), oy + num())
            cur.extend(cubic_points(pos, c1, c2, end)); pos = end; last_c2 = c2
        elif C == "S":
            c1 = (2 * pos[0] - last_c2[0], 2 * pos[1] - last_c2[1]) if last_c2 else pos
            c2 = (ox + num(), oy + num()); end = (ox + num(), oy + num())
            cur.extend(cubic_points(pos, c1, c2, end)); pos = end; last_c2 = c2
        elif C == "Q":
            c = (ox + num(), oy + num()); end = (ox + num(), oy + num())
            c1 = (pos[0] + 2 / 3 * (c[0] - pos[0]), pos[1] + 2 / 3 * (c[1] - pos[1]))
            c2 = (end[0] + 2 / 3 * (c[0] - end[0]), end[1] + 2 / 3 * (c[1] - end[1]))
            cur.extend(cubic_points(pos, c1, c2, end, 6)); pos = end; last_c2 = None
        elif C == "Z":
            if cur:
                subs.append((cur, True))
            cur = None
            pos = start
            # A command after Z without M continues from the start point.
            cur_after = [pos]
            cur = cur_after if i < len(toks) and not re.match(r"[Mm]", toks[i]) else None
            last_c2 = None
        else:
            sys.exit(f"unsupported path command {cmd}")
    if cur and len(cur) > 1:
        subs.append((cur, False))
    elif cur and len(cur) == 1 and subs and subs[-1][0][-1] != cur[0]:
        pass
    return subs


def q(v):
    return int(round(v * SCALE))


def main():
    pts, paths, glyphs = [], [], []
    for name, d, fill in GLYPHS:
        first = len(paths)
        for poly, closed in parse(d):
            start = len(pts) // 2
            for x, y in poly:
                pts += [q(x), q(y)]
            paths.append((start, len(poly), 1 if closed else 0))
        glyphs.append((name, first, len(paths) - first, 1 if fill else 0))
    icons = []
    for name, c0, c1, fg, d in ICONS:
        first = len(paths)
        for poly, closed in parse(d):
            start = len(pts) // 2
            for x, y in poly:
                pts += [q(x), q(y)]
            paths.append((start, len(poly), 1 if closed else 0))
        icons.append((name, first, len(paths) - first, c0, c1, fg))

    o = []
    a = o.append
    a("/* GENERATED by tools/gen-ui-glyphs.py -- do not edit by hand.")
    a(" * v10 system glyphs: polylines in a 24-unit viewbox, Q4 fixed point. */")
    a("#ifndef CAREOS_UI_GLYPH_DATA_H")
    a("#define CAREOS_UI_GLYPH_DATA_H")
    a(f"#define UI_GLYPH_DATA_COUNT {len(glyphs)}")
    a("static const i16 UI_GLYPH_PTS[] = {")
    for k in range(0, len(pts), 16):
        a("    " + ",".join(str(v) for v in pts[k:k + 16]) + ",")
    a("};")
    a("/* {first point, point count, closed} */")
    a("static const u16 UI_GLYPH_PATHS[][3] = {")
    for s, n, c in paths:
        a(f"    {{ {s}, {n}, {c} }},")
    a("};")
    a("/* {first path, path count, filled} in ui_glyph_t order */")
    a("static const u16 UI_GLYPH_DEFS[][3] = {")
    for name, f, n, fill in glyphs:
        a(f"    {{ {f}, {n}, {fill} }},  /* {name} */")
    a("};")
    a(f"#define UI_ICON_DATA_COUNT {len(icons)}")
    a("/* {first path, path count} and {gradient top, gradient bottom, glyph} in ui_icon_t order */")
    a("static const u16 UI_ICON_DEFS[][3] = {")
    for name, f, n, c0, c1, fg in icons:
        a(f"    {{ {f}, {n}, 0 }},  /* {name} */")
    a("};")
    a("static const u32 UI_ICON_COLORS[][3] = {")
    for name, f, n, c0, c1, fg in icons:
        a(f"    {{ 0x{c0:06X}, 0x{c1:06X}, 0x{fg:06X} }},  /* {name} */")
    a("};")
    a("#endif")
    sys.stdout.write("\n".join(o) + "\n")


if __name__ == "__main__":
    main()
