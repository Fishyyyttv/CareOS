"""Tests for tools/repack-cra.py: the QOI encoder must round-trip exactly
(checked with an independent reference decoder) and the archive layout must
survive a read -> repack -> write -> read cycle.

    python3 tools/test_repack_cra.py
"""
import importlib.util
import pathlib
import random
import struct

spec = importlib.util.spec_from_file_location(
    "repack", pathlib.Path(__file__).with_name("repack-cra.py"))
rp = importlib.util.module_from_spec(spec)
spec.loader.exec_module(rp)


def qoi_decode_ref(data):
    """Straight from the QOI spec; returns (w, h, list of (r,g,b,a))."""
    assert data[:4] == b"qoif"
    w, h = struct.unpack(">II", data[4:12])
    px, index = [], [(0, 0, 0, 0)] * 64
    r, g, b, a = 0, 0, 0, 255
    p, end = 14, len(data) - 8
    while len(px) < w * h:
        op = data[p]; p += 1
        if op == 0xFE:
            r, g, b = data[p], data[p + 1], data[p + 2]; p += 3
        elif op == 0xFF:
            r, g, b, a = data[p], data[p + 1], data[p + 2], data[p + 3]; p += 4
        elif op >> 6 == 0:
            r, g, b, a = index[op]
        elif op >> 6 == 1:
            r = (r + ((op >> 4) & 3) - 2) % 256
            g = (g + ((op >> 2) & 3) - 2) % 256
            b = (b + (op & 3) - 2) % 256
        elif op >> 6 == 2:
            b2 = data[p]; p += 1
            vg = (op & 63) - 32
            r = (r + vg - 8 + (b2 >> 4)) % 256
            g = (g + vg) % 256
            b = (b + vg - 8 + (b2 & 15)) % 256
        else:
            for _ in range((op & 63) + 1):
                px.append((r, g, b, a))
            continue
        index[(r * 3 + g * 5 + b * 7 + a * 11) % 64] = (r, g, b, a)
        px.append((r, g, b, a))
    assert p <= end and data[end:] == b"\0" * 7 + b"\1"
    return w, h, px[:w * h]


def bgra_of(pixels):
    return bytes(v for (r, g, b, a) in pixels for v in (b, g, r, a))


def check_roundtrip(w, h, pixels):
    q = rp.qoi_encode(w, h, bgra_of(pixels))
    dw, dh, got = qoi_decode_ref(q)
    assert (dw, dh) == (w, h)
    assert got == pixels, "QOI round-trip mismatch"


def test_qoi_roundtrip_varied():
    rnd = random.Random(1234)
    w, h = 37, 23
    pix = []
    for i in range(w * h):
        kind = i % 5
        if kind == 0:   pix.append((rnd.randrange(256), rnd.randrange(256), rnd.randrange(256), 255))
        elif kind == 1: pix.append(pix[-1])                                   # runs
        elif kind == 2: pix.append((rnd.randrange(256),) * 3 + (rnd.randrange(256),))  # alpha
        elif kind == 3:
            r, g, b, a = pix[-1]; pix.append(((r + 1) % 256, g, (b - 1) % 256, a))   # small diff
        else:           pix.append(pix[max(0, i - 7)])                         # index hits
    check_roundtrip(w, h, pix)


def test_qoi_long_run_and_gradient():
    check_roundtrip(200, 1, [(5, 6, 7, 255)] * 200)                            # runs > 62
    check_roundtrip(64, 4, [(x * 3 % 256, (x + y) % 256, 255 - x, 200) for y in range(4) for x in range(64)])


def test_archive_repack_roundtrip():
    w, h = 8, 8
    raw_px = [(10, 20, 30, 255)] * (w * h)
    raw = bgra_of(raw_px)
    cri = b"CRI1" + struct.pack("<HHBBHI", w, h, rp.ENC_RAW, 0, 0, len(raw)) + raw
    cra = rp.build_cra([("icons/48/x.cri", cri, w, h), ("other", b"not-an-image", 1, 1)])
    entries = rp.read_cra(cra)
    out, before, after = rp.repack(entries)
    assert after < before
    back = rp.read_cra(rp.build_cra(out))
    name, blob, bw, bh = back[0]
    assert name == "icons/48/x.cri" and (bw, bh) == (w, h)
    assert blob[:4] == b"CRI1" and blob[8] == rp.ENC_QOI
    plen = struct.unpack_from("<I", blob, 12)[0]
    _, _, got = qoi_decode_ref(blob[16:16 + plen])
    assert got == raw_px
    assert back[1][1] == b"not-an-image"                                       # untouched


if __name__ == "__main__":
    test_qoi_roundtrip_varied()
    test_qoi_long_run_and_gradient()
    test_archive_repack_roundtrip()
    print("repack OK")
