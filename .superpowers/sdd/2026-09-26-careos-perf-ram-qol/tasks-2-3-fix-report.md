# Tasks 2–3 review fixes

Status: FIXED.

The benchmark now freezes elapsed milliseconds at the 10-second sampling
boundary and uses that duration for FPS after owned-window teardown. A second
start while sampling or closing is rejected with serial and desktop feedback.
The millisecond wait uses wrap-safe `timer_ms()` elapsed subtraction and no
longer adds a PIT tick to every wait.

Window tracking records a monotonic instance ID, so stale slot pointers cannot
be moved or closed after reuse. Each benchmark window is classified as borrowed
or benchmark-owned. Existing windows are snapshotted before any opens change
focus; borrowed geometry, minimized state, and desktop focus are restored after
sampling. Only owned windows are closed. Changed or incomplete window sets
produce an unavailable result, and window/root/buffer allocation failures are
handled without reporting a four-window run. The heap baseline is captured
before opening benchmark-owned windows.

Validation:

- `make -B host-test`: PASS (`qoi_decode OK`, `perf_stats OK`).
- `bash tools/qa/perf-bench.sh`: benchmark run PASS as an accounting/ownership
  regression check; result was `elapsed_ms=10150`, `borrowed_alive=1`, and
  `heap_delta_kb=+9`. The benchmark performance target itself remains FAIL at
  4 FPS / 311 ms p95, as expected before compositor optimization. The script
  now checks the bounded elapsed field and survival of at least one borrowed
  window (QA auto-login's Terminal).
- The later focus restoration adjustment compiled under QA flags. A second QA
  image build was interrupted before replacing the existing `qa/careos-bench.iso`
  or running QEMU, to preserve the controller's VBox input. Plain `make
  careos.iso` was then run to restore the normal ISO.
- `git diff --check`: PASS.

Limitations: the final focus-only adjustment was compile-checked but was not
included in the recorded QEMU/VBox benchmark run. Timer wait behavior is
verified by implementation review and the full builds; no dedicated PIT timing
unit test exists.
