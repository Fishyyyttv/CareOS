import subprocess, sys, pathlib
import pytest

sys.path.insert(0, str(pathlib.Path(__file__).parent))
import importlib
gen = importlib.import_module("gen-font")

TTF = pathlib.Path(__file__).parent.parent / "fonts" / "JetBrainsMono-Regular.ttf"
SIZES = [11, 13, 16, 20, 28]


def test_face_has_95_glyphs():
    face = gen.build_face(str(TTF), 13)
    assert len(face.glyphs) == 95


def test_advance_is_uniform_monospace():
    # build_face() hard-fails on a non-monospace font, so reaching here at all
    # proves uniformity. Pin the actual measured value so a silent metric
    # regression (wrong size, wrong font file) is caught too: JetBrains Mono
    # v2.304 at 13px measures advance=8, ascender=14, height=17.
    face = gen.build_face(str(TTF), 13)
    assert face.advance == 8
    assert face.baseline == 14
    assert face.line_h == 17


def test_non_monospace_font_is_rejected(tmp_path, monkeypatch):
    """build_face must refuse a proportional font rather than silently
    picking one glyph's advance — ~120 layout sites depend on fixed advance."""
    real_load = gen.freetype.Face.load_char
    calls = {"n": 0}

    def fake_load(self, ch, flags):
        real_load(self, ch, flags)
        calls["n"] += 1
        self.glyph.advance.x = (7 + calls["n"] % 2) << 6   # alternate 7/8

    monkeypatch.setattr(gen.freetype.Face, "load_char", fake_load)
    with pytest.raises(SystemExit):
        gen.build_face(str(TTF), 13)


def test_coverage_length_matches_sum_of_glyph_areas():
    face = gen.build_face(str(TTF), 13)
    assert len(face.coverage) == sum(g.w * g.h for g in face.glyphs)


def test_every_glyph_offset_is_in_range():
    face = gen.build_face(str(TTF), 13)
    for g in face.glyphs:
        assert 0 <= g.offset
        assert g.offset + g.w * g.h <= len(face.coverage)


def test_baseline_fits_within_line_height():
    face = gen.build_face(str(TTF), 13)
    assert 0 < face.baseline <= face.line_h


def test_larger_size_has_larger_advance():
    assert gen.build_face(str(TTF), 28).advance > gen.build_face(str(TTF), 11).advance


def test_cli_requires_exactly_five_sizes(tmp_path):
    out = tmp_path / "f.c"
    r = subprocess.run(
        [sys.executable, str(pathlib.Path(__file__).parent / "gen-font.py"),
         str(TTF), "--name", "X", "--symbol", "x", "--sizes", "11,13", "--out", str(out)],
        capture_output=True, text=True)
    assert r.returncode != 0
    assert "exactly 5" in (r.stderr + r.stdout)


def test_cli_rejects_non_ascending_sizes(tmp_path):
    """Slot order maps directly to font_size_t (CAPTION..H1); an inverted
    or non-strictly-increasing list must be rejected, not silently accepted."""
    out = tmp_path / "f.c"
    r = subprocess.run(
        [sys.executable, str(pathlib.Path(__file__).parent / "gen-font.py"),
         str(TTF), "--name", "X", "--symbol", "x",
         "--sizes", "28,20,16,13,11", "--out", str(out)],
        capture_output=True, text=True)
    assert r.returncode != 0
    assert "ascending" in (r.stderr + r.stdout)


def test_cli_rejects_out_of_range_px(tmp_path):
    """f.px is emitted into a u8 struct field and comes straight from
    unvalidated CLI input; an oversized size must not silently truncate."""
    out = tmp_path / "f.c"
    r = subprocess.run(
        [sys.executable, str(pathlib.Path(__file__).parent / "gen-font.py"),
         str(TTF), "--name", "X", "--symbol", "x",
         "--sizes", "11,13,16,20,300", "--out", str(out)],
        capture_output=True, text=True)
    assert r.returncode != 0
    assert "does not fit in u8" in (r.stderr + r.stdout)


def test_glyph_bearing_out_of_i8_range_is_rejected(tmp_path, monkeypatch):
    """bearing_x/bearing_y are i8 struct fields and can legitimately be
    negative; emit_c must range-check them signed, not with _fits_u8."""
    # bitmap_left/bitmap_top are read-only properties on GlyphSlot backed
    # directly by the ctypes struct field (unlike advance, which returns a
    # mutable Vector wrapper) -- so the class-level property itself must be
    # overridden to fake an out-of-range bearing, the same way
    # test_non_monospace_font_is_rejected fakes load_char.
    monkeypatch.setattr(gen.freetype.GlyphSlot, "bitmap_left",
                         property(lambda self: 200))
    face = gen.build_face(str(TTF), 13)
    with pytest.raises(SystemExit):
        gen.emit_c("Test", "test_i8", [face], str(tmp_path / "f.c"), str(TTF))


def test_cli_emits_expected_c_symbols(tmp_path):
    out = tmp_path / "font_x.c"
    r = subprocess.run(
        [sys.executable, str(pathlib.Path(__file__).parent / "gen-font.py"),
         str(TTF), "--name", "Test Face", "--symbol", "test_face",
         "--sizes", "11,13,16,20,28", "--out", str(out)],
        capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    src = out.read_text()
    assert "const font_family_t font_test_face" in src
    assert '"Test Face"' in src
    assert "OFL" in src           # attribution header required
    for px in SIZES:
        assert f"test_face_cov_{px}" in src
        assert f"test_face_glyphs_{px}" in src
