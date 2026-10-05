#!/bin/bash
# Host build of the web engine + webrender harness. Objects go to $OUT (D: scratch).
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=${OUT:-/mnt/d/Users/mhetm/Downloads/CareOS_v9_full/webhost-build}
mkdir -p "$OUT/lexbor" "$OUT/engine"
CF="-O2 -g -I$ROOT/third_party -DLEXBOR_STATIC -w"
for f in $(cd "$ROOT/third_party" && find lexbor -name '*.c'); do
  o="$OUT/lexbor/$(echo $f | tr / _).o"
  if [ ! -f "$o" ] || [ "$ROOT/third_party/$f" -nt "$o" ]; then gcc $CF -c "$ROOT/third_party/$f" -o "$o"; fi
done
EF="-O2 -g -I$ROOT/third_party -DLEXBOR_STATIC -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers -Wno-unused-function"
for f in "$ROOT"/engine/*.c; do
  case "$f" in */math.c) continue;; esac   # CareOS-only libm subset
  gcc $EF -c "$f" -o "$OUT/engine/$(basename $f .c).o"
done
gcc $EF -I"$ROOT/engine" "$ROOT/host/webrender.c" $(ls "$OUT"/engine/*.o | grep -v /math.o) "$OUT"/lexbor/*.o -lm -o "$OUT/webrender"
echo built "$OUT/webrender"
