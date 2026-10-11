# musl math subset

Sources are from the musl 1.2.5 release:
https://musl.libc.org/releases/musl-1.2.5.tar.gz

See `COPYRIGHT` and the notices in individual files. Double-precision operations
needed by QuickJS, float operations needed by stb, and their helper/data files
are built directly into the freestanding kernel. No hosted libc is linked.

Local adaptations: `endian.h` describes the x86-64 target; `features.h` supplies
visibility annotations; `fp_arch.h` is musl's generic version. `lrint.c` wraps the
instruction from musl's x86-64 `lrint.s`. The math declarations in
`include/libc/math.h` come from the same release with hosted type-header includes
replaced by the SSE target's float_t/double_t definitions. Compile with
`-ffp-contract=off`. The original approximate `web/engine/math.c` and libc shim
`fabs` definition are excluded/replaced to prevent duplicate symbols.
