#!/bin/bash
# Host build of the web engine + webrender harness. Objects go to $OUT (D: scratch).
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=${OUT:-$ROOT/../../webhost-build}
EXTRA_CFLAGS=${EXTRA_CFLAGS:-}
mkdir -p "$OUT/lexbor" "$OUT/engine" "$OUT/quickjs"

if [ ! -f "$ROOT/engine/js_prelude.h" ] || [ "$ROOT/engine/prelude.js" -nt "$ROOT/engine/js_prelude.h" ]; then
  bash "$ROOT/host/gen_prelude.sh"
fi

CF="-O2 -g $EXTRA_CFLAGS -I$ROOT/third_party -DLEXBOR_STATIC -w"
for f in $(cd "$ROOT/third_party" && find lexbor -name '*.c'); do
  o="$OUT/lexbor/$(echo $f | tr / _).o"
  if [ ! -f "$o" ] || [ "$ROOT/third_party/$f" -nt "$o" ]; then gcc $CF -c "$ROOT/third_party/$f" -o "$o"; fi
done

QF="-O2 -g $EXTRA_CFLAGS -D_GNU_SOURCE -w"
for f in quickjs.c libregexp.c libunicode.c dtoa.c; do
  o="$OUT/quickjs/${f%.c}.o"
  if [ ! -f "$o" ] || [ "$ROOT/third_party/quickjs/$f" -nt "$o" ]; then gcc $QF -c "$ROOT/third_party/quickjs/$f" -o "$o"; fi
done

EF="-O2 -g $EXTRA_CFLAGS -I$ROOT/third_party -DLEXBOR_STATIC -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers -Wno-unused-function"
for f in "$ROOT"/engine/*.c; do
  case "$f" in */math.c) continue;; esac   # CareOS-only libm subset
  gcc $EF -c "$f" -o "$OUT/engine/$(basename $f .c).o"
done

gcc $EF -I"$ROOT/engine" "$ROOT/host/webrender.c" $(ls "$OUT"/engine/*.o | grep -v /math.o) "$OUT"/quickjs/*.o "$OUT"/lexbor/*.o -lm -o "$OUT/webrender"
echo built "$OUT/webrender"
