#!/usr/bin/env bash
# Boot the current careos.iso headless N times and report desktop arrivals.
# Run in WSL from the repo root: bash tools/qa/boot-check.sh [runs=5] [mem=2048M]
set -uo pipefail
RUNS=${1:-5}; MEM=${2:-2048M}
OUT=${QA_OUT:-../qa}; mkdir -p "$OUT"
Q=
cleanup() {
  if [[ -n "$Q" ]]; then
    kill "$Q" 2>/dev/null || true
    wait "$Q" 2>/dev/null || true
    Q=
  fi
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
ok=0
for i in $(seq 1 "$RUNS"); do
  LOG="$OUT/boot-$i.log"; : > "$LOG"
  timeout -k 5s 240s qemu-system-x86_64 -m "$MEM" -smp 2 -cpu max -accel tcg,thread=multi \
    -cdrom careos.iso -vga std -display none -serial file:"$LOG" -no-reboot \
    -machine pc,usb=off -drive file=careos.img,format=raw,if=ide,index=0,snapshot=on \
    -netdev user,id=net0 -device e1000,netdev=net0 &
  Q=$!
  for _ in $(seq 1 48); do
    grep -q 'entering main loop' "$LOG" && break
    kill -0 "$Q" 2>/dev/null || break
    sleep 5
  done
  if grep -q 'entering main loop' "$LOG"; then
    ok=$((ok+1)); echo "run $i: desktop"
  else
    echo "run $i: FAILED"; tail -5 "$LOG"
  fi
  cleanup
done
echo "boot-check: $ok/$RUNS reached the desktop"
[[ "$ok" -eq "$RUNS" ]]
