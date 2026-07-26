#!/usr/bin/env python3
"""Emit the Classic family: the original 8x12 bitmap, integer-scaled to the
five font_size_t slots using the same factors gfx_font_scale_for() used
(1x for CAPTION/BODY/H3, 2x for H2/H1). Coverage is 0 or 255 only, so it
flows through the same blended render path as the TrueType families.
"""
import importlib.util
import pathlib
import sys

HERE = pathlib.Path(__file__).parent
spec = importlib.util.spec_from_file_location("genfont", HERE / "gen-font.py")
gen = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gen)

from classic_glyphs import CLASSIC

# (nominal px label, integer scale) per font_size_t slot
SLOTS = [(11, 1), (13, 1), (16, 1), (20, 2), (28, 2)]
CELL_W, CELL_H = 8, 12


def build_scaled_face(scale: int, px_label: int) -> gen.Face:
    glyphs, coverage = [], bytearray()
    w, h = CELL_W * scale, CELL_H * scale
    for rows in CLASSIC:
        offset = len(coverage)
        for row in range(h):
            bits = rows[row // scale]
            for col in range(w):
                lit = (bits >> (7 - (col // scale))) & 1
                coverage.append(255 if lit else 0)
        # bearing_y = h places pixel row 0 at cell top, matching the old
        # renderer which drew rows 0..11 downward from y.
        glyphs.append(gen.Glyph(w, h, 0, h, offset))
    return gen.Face(px=px_label, advance=w, line_h=h + scale,
                    baseline=h, glyphs=glyphs, coverage=coverage)


def main() -> None:
    faces = [build_scaled_face(scale, px) for px, scale in SLOTS]
    out = str(HERE.parent / "gui" / "fonts" / "font_classic.c")
    gen.emit_c("Classic 8x12", "classic", faces, out,
               "original CareOS bitmap font (tools/classic_glyphs.py)")
    print(f"{out}: written")


if __name__ == "__main__":
    main()
