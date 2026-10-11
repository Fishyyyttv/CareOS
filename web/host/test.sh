#!/bin/bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
export OUT=${OUT:-$ROOT/../webhost-test}
export EXTRA_CFLAGS=${EXTRA_CFLAGS:--O1 -fsanitize=address,undefined -fno-omit-frame-pointer}
bash "$ROOT/web/host/build.sh"
objects=()
for o in "$OUT"/engine/*.o; do [[ "$o" == */math.o ]] || objects+=("$o"); done
gcc -g $EXTRA_CFLAGS -I"$ROOT/web/engine" -I"$ROOT/web/third_party" \
    "$ROOT/tests/host/test_web_js.c" "${objects[@]}" "$OUT"/quickjs/*.o "$OUT"/lexbor/*.o -lm -o "$OUT/test_web_js"
ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=1} "$OUT/test_web_js"
