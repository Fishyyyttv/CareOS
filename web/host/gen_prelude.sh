#!/bin/bash
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
python3 -c '
import sys
with open("'"$ROOT"'/engine/prelude.js", "rb") as f:
    data = f.read()
with open("'"$ROOT"'/engine/js_prelude.h", "w") as f:
    f.write("/* Generated from web/engine/prelude.js -- do not edit directly. */\n")
    f.write("#ifndef JS_PRELUDE_H\n#define JS_PRELUDE_H\n\n")
    f.write("static const char JS_PRELUDE[] = {\n")
    for i in range(0, len(data), 16):
        chunk = data[i:i+16]
        f.write("  " + ", ".join(f"0x{b:02x}" for b in chunk) + ",\n")
    f.write("  0x00\n};\n\n#endif\n")
'
echo "Generated $ROOT/engine/js_prelude.h"
