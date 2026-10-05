# Weave JavaScript integration

Weave embeds QuickJS-ng in both the host renderer and the CareOS kernel. This is
an experimental browser DOM implementation, not a complete web platform.

## Execution model

Each page owns a QuickJS runtime and context. The HTML document is parsed first;
up to 48 classic inline/external scripts then execute in document order. External
scripts use the embedding's existing fetch callback. Modules, dynamic script
insertion, and true parser-blocking/async/defer scheduling are not implemented.
DOMContentLoaded and load callbacks run after that initial script pass, before
the first layout. Geometry reads during initial scripts therefore return zeros.

`js.c` binds Lexbor nodes to stable JavaScript wrappers. `prelude.js` supplies
convenience APIs and events; `gen_prelude.sh` embeds it as `js_prelude.h`. Detached
DOM nodes and wrapper identities remain alive until the document is destroyed.
DOM changes trigger a stylesheet reapplication and a rebuilt box tree. Focused
text inputs are recovered by DOM identity after the rebuild. Scripts can mutate
attributes, classes and inline styles; changing style/link elements after load
does not currently reload the author stylesheets.

Initial page scripts execute in the browser fetch worker. Once handed to the
desktop, events, timers, promise jobs and page destruction run on the desktop
thread. The worker only continues image loading. Both the desktop bootstrap
stack and task stacks are 2 MiB; QuickJS's stack allowance is 1 MiB. The desktop
polls timers through `web_page_step` and consumes navigation/scroll requests.
Script execution can pause desktop input for its execution budget.

## Implemented surface

- Classic JavaScript, JSON, promises, typed arrays and QuickJS built-ins.
- DOM node traversal, creation, cloning, insertion/removal and text/HTML replacement.
- ID/tag/class lookup, selectors, attributes, classList, dataset and inline style.
- Click/input/change/submit callbacks, bubbling, inline handlers, default prevention.
- Text/textarea editing, checkboxes/radios, cycling selects and GET form submission.
- Timeouts, intervals, animation-frame callbacks and bounded promise-job draining.
- Basic URL/search-parameter helpers, location navigation, title and scrolling.
- Console output through the host log or kernel serial port.
- Page-local in-memory storage and limited computed-style/geometry accessors.

Many compatibility objects in the prelude are placeholders: observers,
custom elements, selection, canvas/media, workers, WebSocket, and related APIs do
not provide their full browser semantics. Fetch/XHR do not perform network
requests. Cookie persistence, cross-page storage, service workers, module loading,
and a browser security/origin model are absent. Event capture and full keyboard,
pointer and focus event semantics are not implemented. Form methods currently use
the existing GET path. Secure random APIs explicitly fail because no entropy
provider is connected to this embedding.

## Limits and platform support

QuickJS is limited to 64 MiB of its own allocations per page, 256 timers, a
4-second initial script budget, a separate 500 ms load-event budget and 300 ms per
event/timer batch. A batch drains at most 1,000 promise jobs; remaining jobs are
processed on later ticks. The embedding must provide a monotonic clock or scripts
are disabled. These limits do not cap Lexbor/layout/image allocations or the
time spent inside synchronous native callbacks such as resource fetching.

This runs in kernel address space with the rest of Weave. Execution limits are
not a security sandbox. Existing TLS certificate-validation limitations still
apply. Scripts can consume memory through DOM mutations until a page is closed.

The kernel port disables QuickJS threading/Atomics support, uses the kernel heap,
reads the RTC as UTC for Date, and uses the PIT clock for execution deadlines.
`kernel/quickjs_platform.c` contains the platform hooks. A licensed musl 1.2.5
math subset replaces the renderer's approximate math functions in the kernel
link, providing JavaScript's floating-point operations and edge cases. The old
`web/engine/math.c` is retained for history but is excluded from the build.

## Build and validation

Run these from `CareOS_v9` in Linux/WSL:

```sh
make -j8 all
bash web/host/test.sh
python3 tests/run_web.py
```

The host suite uses AddressSanitizer and UndefinedBehaviorSanitizer. Its objects
default to the sibling `webhost-test` directory; use a fresh `OUT` directory when
changing instrumentation. The QEMU suite creates a separate test ISO under
`build/web-smoke`, attaches no user disk, and boots with `web-selftest`. Normal
boots never execute the test suite. Both exercise DOM reference lifetime,
script-driven restyling, focus retention, submission cancellation, external
scripts, lifecycle events, memory limits, timers, navigation and interruption
of an infinite promise callback. The QEMU run also exercises the kernel libc,
math library, heap accounting and bootstrap stack.

For visual inspection:

```sh
bash web/host/build.sh
WEB_ACTIONS='click:30,145;step' WEB_DUMP=1 \
  ../webhost-build/webrender web/host/fixtures/js_test.html /tmp/weave-js.png 800 600
```

`WEB_ACTIONS` also accepts `type:...`, `key:enter`, `key:tab`, `key:esc`, `key:bs`,
and `wait:milliseconds`. Host rendering does not test the kernel network adapter
or real hardware.
