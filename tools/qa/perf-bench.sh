#!/usr/bin/env bash
# Build a QA ISO, run the deterministic 10 second compositor benchmark, then
# restore the normal ISO. Run from the repository root in WSL.
set -euo pipefail
MEM=${1:-2048M}
OUT=${QA_OUT:-../qa}
mkdir -p "$OUT"
Q=""
cleanup() {
  if [[ -n "$Q" ]]; then kill "$Q" 2>/dev/null || true; wait "$Q" 2>/dev/null || true; fi
}
trap cleanup EXIT INT TERM
make PERF_BENCH=1 QA_AUTOLOGIN=1 careos.iso
cp careos.iso "$OUT/careos-bench.iso"
make careos.iso
LOG="$OUT/perf-bench.log"
: > "$LOG"
timeout -k 5s 900s qemu-system-x86_64 -m "$MEM" -smp 2 -cpu max -accel tcg,thread=multi \
  -cdrom "$OUT/careos-bench.iso" -vga std -display none -serial file:"$LOG" -no-reboot \
  -machine pc,usb=off -drive file=careos.img,format=raw,if=ide,index=0,snapshot=on \
  -netdev user,id=net0 -device e1000,netdev=net0 &
Q=$!
while ! grep -q '\[perf-bench\] RESULT' "$LOG" 2>/dev/null; do
  kill -0 "$Q" 2>/dev/null || { tail -40 "$LOG"; exit 1; }
  sleep 5
done
grep '\[perf-bench\]' "$LOG"
grep -Eq '\[perf-bench\] RESULT elapsed_ms=1[0-9]{4} .*borrowed_alive=[1-4] ' "$LOG" || {
  echo "perf-bench did not report a bounded active duration and preserved borrowed window" >&2
  exit 1
}
