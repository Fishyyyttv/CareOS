# Window chrome implementation

Implemented a focused part of phase 1 against `Terminal.dc.html` and
`SettingsLook.dc.html`. This does not mark W0 or the full shell redesign complete.

- 48 px title bar, 22 px window corners, left-side red/yellow/green controls
  with 14 px visible lights and separate 22 x 28 px input targets.
- Dark/light chrome palettes in `gui/theme.c`, bold titles, inactive controls
  and titles, hover glyphs, and a one-pixel border and title divider.
- Drawing, hover, and click handling share `gui/window_chrome.h` geometry.
  Resize feedback excludes control targets.
- The snap menu now anchors below the green light, stays open while the pointer
  crosses into it, and maps each preview to its actual layout. Snap animation
  completes when the remaining distance is smaller than the integer step.
- Legacy app fills are masked back to the rounded window silhouette using four
  reusable 22 x 22 backdrop tiles (7,744 bytes), including overlapping windows.
  This uses the same row geometry as the existing cached-window blitter.

Integration notes for the remaining redesign:

- Chrome currently uses opaque colours. Real backdrop blur and glass caching
  remain part of the planned shared UI kit work.
- Titles use the user's current bold font. Figtree/font roles and app-specific
  titlebar toolbars remain pending.
- The small `wm_chrome_palette_t` table deliberately leaves legacy app colours
  and persisted accent indices alone. Fold it into the shared tokens/UI kit
  when that migration lands; the control positions are relative to the window.

Validation: normal and QA ISO builds, all 15 sanitizer-enabled host tests, and
QEMU input/screenshot checks. QA outputs are under the workspace's
`qa/window-chrome/`; the normal `careos.iso` retains the login gate.
