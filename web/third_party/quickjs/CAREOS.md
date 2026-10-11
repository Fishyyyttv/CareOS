# CareOS port notes

The vendored QuickJS-ng engine consists of quickjs.c, libregexp.c, libunicode.c,
dtoa.c and their headers. Version macros are in quickjs.h; the upstream license
is in LICENSE and the source headers.

`CAREOS` selects kernel time/allocator hooks in cutils.h, disables hosted thread
and process dependencies, uses UTC for the Date timezone, maps alloca to the
compiler builtin, and parses JSON numbers with the bundled js_atod converter.
The Makefile disables Atomics for this port. The hosted build retains upstream
platform behavior. `quickjs-libc` and its OS/file/process modules are not linked
or exposed to page scripts. See docs/weave-javascript.md for embedding limits.
