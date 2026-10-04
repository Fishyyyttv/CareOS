# Phase 2 (start screens) — progress

Spec: `docs/superpowers/specs/2026-10-03-careos-start-screens-design.md`.
Code: `gui/session/` (boot, login, signup, power, onboarding, common). QA
screenshots and the interactive driver: `qa/v10-shell/` (`drive.py`,
`fresh-boot.sh`, `reboot-check.sh`, `splash-check.sh`).

## Done

| Spec § | What |
|---|---|
| 1 Boot | Ground, pre-rendered coral glow + calm rings, beating heart, "CareOS", progress track, status, mono footer. Progress is **real**: rendering the wallpaper, rasterising the app icons, checking the network. |
| 2 Login | Wallpaper, 120 px Bricolage clock (new digits-only face), date, 104 px gradient avatar with ring, display name, glass password field with the coral submit, Forgot password sheet, account pills (+ Other user when > 7), Guest, New account, Power. Wrong password shakes and explains; the hint appears after 2 failures; kernel lockout shown as a wait. Bootstrap passwords still force a change (two-field variant). |
| 3 Sign up | Light card over Care Day: CareOS panel with the 3-step rail; avatar colour (6 gradients), name, username (validated live, home-folder hint), password (show/hide, 4-bar meter), optional hint. Creates, signs in, saves profile + hint, onboarding continues at step 2. |
| 4 Lock | Blurred dimmed wallpaper, "Locked", date, 168 px clock, the two newest real notifications (app, time, title — no body), Pip slot, "Press any key or click to unlock", Power. Triggers: CareOS menu, Control Center, Super+L, idle. |
| 5 Power | Modal over the blurred screen: "Taking a break?", Restart, Shut down (coral), Log out (in a session), Cancel; Esc / outside closes. From the CareOS menu (Power…), the Control Center and the login screen. |
| 6 Onboarding | 1000×660 glass card, 4-segment progress, Back / Skip / Continue, Esc → "Finish setup later?". Welcome (English only, Region = time zone, US QWERTY), Get online (real Ethernet status), Pick your vibe (Light / Dark / Auto applied live, accents), All set (real summary, tips). Step saved per user, resumes after a reboot. |

## Persistence added

- **Settings v7**: theme mode (dark/light/auto), glass strength, reduce
  transparency, dock position/autohide, brightness, Do Not Disturb, **time
  zone offset** (default UTC-7 — the offset that was hard-coded before; both
  clocks now use the setting). Older blobs migrate with today's behaviour.
  Brightness and Focus now persist.
- **User DB v7**: display name, avatar colour, onboarded, onboarding step
  (253-byte records so 16 users still fit the 8-sector region). Existing
  accounts migrate as onboarded; new accounts (and the bootstrap ones on a
  fresh disk) onboard on first sign-in. Password hints live in
  `/home/.careos-hints` (they must be readable before sign-in and would not
  fit the record region).
- `kernel/account_rules.c` (host-tested): username rules, strength bands,
  onboarding step machine, auto-theme hours.

## Deviations (real data or hide)

- No "Hold Shift for recovery options" (no recovery mode exists), no Sleep
  tile, no "Reopen my windows" (no session restore), no power-button
  countdown (no ACPI button events), no "Use PIN instead" (no PINs).
- Sign up has no "Make this the administrator account": at sign-up nobody is
  signed in and root always exists, so the spec's condition can never hold.
- Login has no Accessibility or network buttons (no text-size setting; the
  network is shown in onboarding and the menu bar).
- Onboarding: one language, one keyboard layout; Region offers fixed UTC
  offsets labelled as such (no DST data). Text-size slider hidden.
- Tips mention Super and the dock, not Ctrl+Space (removed in phase 1 work).

## Verified (QEMU, normal ISO, blank disk)

Boot splash → login → wrong password → bootstrap password → forced change →
onboarding (all 4 steps, theme switched live) → desktop → Super+L lock →
unlock → log out → Sign up of a new account → onboarding from step 2 →
power menu → Restart → login shows the new account (last sign-in), its
display name and avatar, the hint after 2 failures → sign-in skips
onboarding. Host tests 22/22, window-chrome regression 8/8, normal ISO stops
at the login gate.
