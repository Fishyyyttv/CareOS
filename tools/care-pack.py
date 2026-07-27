#!/usr/bin/env python3
"""Build a .care package, base64-encoding any file that is not plain text.

  python3 tools/care-pack.py apps-src/browser --out browser.care \
      --set name=browser --set version=1.0 --set exec=main \
      --set icon=icon.cri --set category=Internet

Every file under the source directory becomes a section. Text files use FILE
and keep their line structure; binary files use FILEB64, which is the only way
an icon can survive a format whose section bodies are newline-delimited (see
the header comment in kernel/carepkg.c).

Fields not given with --set are inferred where that is unambiguous: `name` from
the directory, `exec` from a file called main/run/start, and `icon` from an
icon.* file sitting in the package. Anything still missing that the installer
needs is reported rather than guessed at.
"""
import argparse
import base64
import sys
from pathlib import Path

# Extensions that are binary regardless of what the content sniffer thinks --
# a .cri that happens to contain no high bytes is still not a text file.
BINARY_EXTS = {".cri", ".cra", ".bmp", ".tga", ".png", ".jpg", ".jpeg",
               ".gif", ".ico", ".bin", ".o", ".elf", ".wad"}

MANIFEST_ORDER = ("name", "version", "description", "author", "exec", "icon",
                  "category", "permissions", "deps")

B64_LINE = 76          # keeps manifests diffable and under carepkg's 256-char
                       # line buffer with room to spare

# Line prefixes carepkg's parser treats as directives. A text payload whose own
# content starts a line with one of these would be read as the end of the
# section (or the start of another file) and silently truncate the package.
DIRECTIVES = ("FILE ", "FILEB64 ", "---ENDFILE---", "---END---")


def looks_like_directive(text: str) -> bool:
    return any(line.startswith(DIRECTIVES) for line in text.split("\n"))


def is_binary(path: Path) -> bool:
    if path.suffix.lower() in BINARY_EXTS:
        return True
    data = path.read_bytes()
    if b"\0" in data:
        return True
    try:
        data.decode("utf-8")
    except UnicodeDecodeError:
        return True
    return False


def build(src: Path, fields: dict):
    """Returns (package_text, text_section_count, binary_section_count)."""
    out = ["CARE 1.0"]
    for key in MANIFEST_ORDER:
        if fields.get(key):
            out.append(f"{key}={fields[key]}")
    out.append("---FILES---")

    n_text = n_binary = 0
    for path in sorted(src.rglob("*")):
        if not path.is_file():
            continue
        rel = path.relative_to(src).as_posix()
        if rel == "manifest.care":
            continue                    # carepkg writes this itself on install

        binary = is_binary(path)
        text = None
        if not binary:
            text = path.read_text(encoding="utf-8").rstrip("\n")
            # A text file that contains a directive at the start of a line would
            # be cut short by the installer's parser. Ship it base64 instead:
            # the encoding cannot produce a directive, so the payload arrives
            # byte-for-byte. This file's own README is exactly such a case.
            if looks_like_directive(text):
                binary = True

        if binary:
            blob = base64.b64encode(path.read_bytes()).decode("ascii")
            out.append(f"FILEB64 {rel}")
            out += [blob[i:i + B64_LINE] for i in range(0, len(blob), B64_LINE)]
            n_binary += 1
        else:
            # Trailing newlines would come back as blank lines on install,
            # because every section line is re-joined with '\n'.
            out.append(f"FILE {rel}")
            out += text.split("\n")
            n_text += 1
        out.append("---ENDFILE---")

    out.append("---END---")
    return "\n".join(out) + "\n", n_text, n_binary


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("srcdir", type=Path)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--set", action="append", default=[], metavar="KEY=VALUE",
                    help="manifest field; repeatable")
    args = ap.parse_args()

    if not args.srcdir.is_dir():
        return f"not a directory: {args.srcdir}"

    fields = {}
    for item in args.set:
        if "=" not in item:
            return f"--set needs KEY=VALUE, got {item!r}"
        k, v = item.split("=", 1)
        if k not in MANIFEST_ORDER:
            return f"unknown manifest field {k!r}; known: {', '.join(MANIFEST_ORDER)}"
        fields[k] = v

    fields.setdefault("name", args.srcdir.resolve().name)
    fields.setdefault("version", "1.0")

    if "exec" not in fields:
        for cand in ("main", "run", "start"):
            if (args.srcdir / cand).is_file():
                fields["exec"] = cand
                break
    if "icon" not in fields:
        for cand in sorted(args.srcdir.glob("icon.*")):
            fields["icon"] = cand.name
            break

    missing = [k for k in ("name", "exec") if not fields.get(k)]
    if missing:
        return (f"missing required field(s): {', '.join(missing)} "
                f"-- pass them with --set")

    text, n_text, n_binary = build(args.srcdir, fields)
    args.out.write_bytes(text.encode("utf-8"))

    print(f"wrote {args.out}: {n_text} text + {n_binary} binary section(s), "
          f"{len(text) / 1024:.1f} KiB")
    if not fields.get("icon"):
        print("note: no icon= field; the launcher will draw the vector glyph")
    return 0


if __name__ == "__main__":
    sys.exit(main())
