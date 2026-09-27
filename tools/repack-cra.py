#!/usr/bin/env python3
"""Re-encode every .cri entry of a .cra icon archive as QOI (CRI_ENC_QOI = 2)
when that is smaller than its current payload. Needs no SVG sources and no
third-party packages.

    repack-cra.py IN.cra OUT.cra            write the repacked archive
    repack-cra.py IN.cra --measure          only report the size change
"""
import struct
import sys

CRI_HDR, CRA_HDR, CRA_ENT, NAME_MAX = 16, 16, 60, 48
ENC_RAW, ENC_RLE, ENC_QOI = 0, 1, 2


def decode_cri(blob):
    """Return (w, h, flags, bgra_bytes) for raw/RLE entries, else None."""
    if len(blob) < CRI_HDR or blob[:4] != b"CRI1":
        return None
    w, h, enc, fl, _r, plen = struct.unpack_from("<HHBBHI", blob, 4)
    p = blob[CRI_HDR:CRI_HDR + plen]
    n = w * h * 4
    if enc == ENC_RAW:
        raw = bytes(p[:n])
    elif enc == ENC_RLE:
        out, i = bytearray(), 0
        while len(out) < n and i < len(p):
            ctl = p[i]; i += 1
            run = (ctl & 0x7F) + 1
            if ctl & 0x80:
                out += p[i:i + 4] * run; i += 4
            else:
                out += p[i:i + run * 4]; i += run * 4
        raw = bytes(out[:n])
    else:
        return None
    if len(raw) != n:
        return None
    return w, h, fl, raw


def qoi_encode(w, h, bgra):
    """Standard QOI (channels=4, sRGB) from B,G,R,A pixel bytes."""
    out = bytearray(b"qoif" + struct.pack(">II", w, h) + bytes([4, 0]))
    index = [(0, 0, 0, 0)] * 64
    pr, pg, pb, pa = 0, 0, 0, 255
    run = 0
    npx = w * h
    for k in range(npx):
        b, g, r, a = bgra[k * 4:k * 4 + 4]
        if (r, g, b, a) == (pr, pg, pb, pa):
            run += 1
            if run == 62 or k == npx - 1:
                out.append(0xC0 | (run - 1)); run = 0
            continue
        if run:
            out.append(0xC0 | (run - 1)); run = 0
        h6 = (r * 3 + g * 5 + b * 7 + a * 11) % 64
        if index[h6] == (r, g, b, a):
            out.append(h6)
        else:
            index[h6] = (r, g, b, a)
            if a == pa:
                dr = (r - pr + 128) % 256 - 128
                dg = (g - pg + 128) % 256 - 128
                db = (b - pb + 128) % 256 - 128
                drg, dbg = dr - dg, db - dg
                if -2 <= dr <= 1 and -2 <= dg <= 1 and -2 <= db <= 1:
                    out.append(0x40 | (dr + 2) << 4 | (dg + 2) << 2 | (db + 2))
                elif -32 <= dg <= 31 and -8 <= drg <= 7 and -8 <= dbg <= 7:
                    out += bytes([0x80 | (dg + 32), (drg + 8) << 4 | (dbg + 8)])
                else:
                    out += bytes([0xFE, r, g, b])
            else:
                out += bytes([0xFF, r, g, b, a])
        pr, pg, pb, pa = r, g, b, a
    out += b"\0" * 7 + b"\1"
    return bytes(out)


def read_cra(data):
    if data[:4] != b"CRA1":
        raise ValueError("not a CRA1 archive")
    count, _total, _ = struct.unpack_from("<III", data, 4)
    entries = []
    for i in range(count):
        e = CRA_HDR + i * CRA_ENT
        name = data[e:e + NAME_MAX].rstrip(b"\0").decode()
        off, ln, w, h = struct.unpack_from("<IIHH", data, e + NAME_MAX)
        entries.append((name, data[off:off + ln], w, h))
    return entries


def build_cra(entries):
    """Same layout as tools/gen-icons.py:build_cra (must match gui/resource_cache.h)."""
    table_bytes = CRA_HDR + len(entries) * CRA_ENT
    offset = (table_bytes + 3) & ~3
    table, blob = bytearray(), bytearray()
    for name, payload, w, h in entries:
        blob += b"\0" * ((-len(blob)) & 3)
        table += name.encode().ljust(NAME_MAX, b"\0")
        table += struct.pack("<IIHH", offset + len(blob), len(payload), w, h)
        blob += payload
    return bytes(b"CRA1" + struct.pack("<III", len(entries), offset + len(blob), 0)
                 + table + b"\0" * (offset - table_bytes) + blob)


def repack(entries):
    out, before, after = [], 0, 0
    for name, blob, w, h in entries:
        payload = blob
        dec = decode_cri(blob)
        if dec:
            cw, ch, fl, raw = dec
            q = qoi_encode(cw, ch, raw)
            if len(q) + CRI_HDR < len(blob):
                payload = b"CRI1" + struct.pack("<HHBBHI", cw, ch, ENC_QOI, fl, 0, len(q)) + q
        before += len(blob); after += len(payload)
        out.append((name, payload, w, h))
    return out, before, after


def main(argv):
    if len(argv) < 3:
        print(__doc__); return 2
    entries = read_cra(open(argv[1], "rb").read())
    out, before, after = repack(entries)
    saving = 100 - after * 100 // max(before, 1)
    print(f"entries={len(entries)} payload_before={before} payload_after={after} saving={saving}%")
    if argv[2] != "--measure":
        open(argv[2], "wb").write(build_cra(out))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
