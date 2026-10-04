# Phase 2 — Start Screens Design

**Date:** 2026-10-03 · Part of the v10 roadmap (`2026-10-03-careos-v10-redesign-roadmap.md`)
**Reference artboards** (`docs/design/careos-v10/`): `Boot`, `Login`, `Signup`, `Lock`, `Power`, `Welcome`, `OnboardWifi`, `OnboardLook`, `OnboardReady`.

## Goal

Everything between power-on and the desktop, and between the desktop and power-off, matches the canvas. It's built only from the phase 1 kit (`gui/ui.h`, tokens, font roles) frozen at W0.

## Rules
- **Real data or hide** (see the roadmap).
- **Pip is phase 4.** The Lock and Power artboards show Pip. Phase 2 leaves a named slot (`session_pet_slot(rect, mood)`) that draws nothing until phase 4 fills it. The layouts don't shift when Pip appears.
- Behaviour is kept from today's `run_login_flow`:
  - PBKDF2 credential checks (`kernel/users.c`)
  - guest session
  - password change
  - account creation
  - lock mode
  - screensaver
- Only the presentation and the new onboarding flow change.

## Files (owned per roadmap)
- start-a: `gui/session/boot_splash.c`, `gui/session/login.c` (login, greeter, sign up, screensaver), `gui/session/lock.c`, `gui/session/power_menu.c`
- start-b: `gui/session/onboarding.c`, first-run state and the onboarding settings in `kernel/settings.c` (marked block)

## 1. Boot splash — `Boot`
- Ground `#070A16`. Coral glow (640 px circle, α .14, blur 110) behind the centre, and two thin calm rings (α .10 / .08). Both are pre-rendered once into a buffer at splash start.
- "CareOS" in Display 64 extra-bold, then a 240×6 progress track (raised) with a care fill driven by **real init progress** (the `done` steps the existing `draw_boot_splash` already counts), then the status line in UI 15 muted, also from the real init step ("Starting network…", "Mounting disk…", …, "Waking things up…").
- Footer in Mono 13, `#7D86A3`: left "v<version> · x86_64", right "Hold Shift for recovery options".
- Holding Shift during the splash sets a recovery flag (boot into a safe mode: default theme, no rc scripts, `smp=off`). The flag is read by `kernel_main`, and the safe-mode path already exists for `rc_care` skipping.
- The heart "beat" animation is a 1.0 → 1.06 scale pulse on a 2 s loop, drawn integer-scaled from the pre-rendered logo.

## 2. Login — `Login`
- Desktop wallpaper (Care Night/Day or the user's wallpaper), no menu bar.
- Clock: Display 120 bold, centred, top 96, with the date (UI 20 medium) below it.
- Centre column (400 wide, top 400):
  - 104 px avatar circle: the gradient from the user's avatar colour; initial in Display 40 extra-bold; 5 px white-12 % ring + shadow
  - username in Display 28 bold
  - a 56 px glass password field (radius 18) with a 44 px care "→" submit button inside its right end
  - links: "Forgot password?" (opens a sheet that explains an administrator can reset it in Users), plus "Use PIN instead" **only if** the account has a PIN (hidden otherwise; adding PINs isn't in this phase)
- Bottom-left user pills (52 px, radius 26): one per account (avatar + name; the selected one is lighter), then "Guest" (teal avatar) if guest is enabled, then a dashed "New account" pill that opens Sign up.
- Bottom-right: Accessibility (text size toggle) and Power (opens the Power menu) icon buttons.
- **Errors:** a wrong password shakes the field (an 8 px horizontal shake, 300 ms) and shows "That password didn't work. Try again." under it in danger. A lockout shows the remaining wait time.
- The success fade to the desktop is kept.

## 3. Sign up — `Signup`
- **Light** card layout (as drawn), over the light Care Day wallpaper, regardless of the current theme.
- Left panel:
  - CareOS logo + "Make this computer yours." (Display) and the explainer copy
  - a 3-step rail: 1 Create account (current), 2 Connect to the network, 3 Pick your look. Steps 2 and 3 are the onboarding steps that follow automatically.
- Form:
  - avatar preview + **avatar colour** swatches (6 gradients; stored in the user profile, a new field)
  - Your name (display name)
  - Username, with the live hint "Your home folder will be /home/\<username\>" and validation (lowercase a–z0–9_-, 1–24 characters, unique)
  - Password with a show/hide toggle and a strength meter (Weak/OK/Strong, using the existing `password_is_strong` rules)
  - Password hint (optional; shown on the login screen after 2 failures)
  - "Make this the administrator account" checkbox, only shown/enabled when the current user is an admin or no admin exists yet
  - Cancel / Create account (primary)
- "Already have one? Sign in" returns to Login.
- After creating an account the user is logged in and onboarding starts at step 2. The first account ever created goes through all 4 steps.

## 4. Lock — `Lock`
- Wallpaper blurred and dimmed.
- Top bar: "Locked" with a lock glyph on the left; battery on the right only if present.
- Date (UI 20) and time (Display 120) centred.
- **Notification previews:** up to 2 glass cards (360 wide) of the real most-recent notifications, showing app, time and title only (no body, for privacy).
- Pip slot (phase 4).
- Footer hint: "Press any key or click to unlock". There's no swipe input, so the copy is adapted.
- Any key or click → the Login card for the current user, in lock mode (existing `run_login_flow(mouse, true)`).
- Lock triggers: the menu-bar logo menu › Lock, Control Center Lock tile, Super+L, and the idle timeout (an existing setting, if present).

## 5. Power menu — `Power`
- A dimmed, blurred backdrop over the current screen, with a centred glass card.
- Header: "Taking a break?" (Display) + subtitle "Your open windows are saved." **only if** session restore exists. Otherwise "Pick what you'd like to do."
- Four 1:1 tiles, each with a glyph and label:
  - Sleep: shown only if ACPI S3 works, so hidden in this phase
  - Restart
  - Shut down (danger-tinted)
  - Log out
- Checkbox "Reopen my windows when I come back": shown only if session restore exists, so hidden in this phase.
- Countdown: "Shutting down in 60 s if you don't pick" with Cancel, only when the menu was opened by a hardware power-button event (ACPI). Opened from the UI, there is no countdown.
- Esc / clicking outside closes it. Restart and Shut down use the existing `power_reboot` / `power_shutdown` (the FADT path from phase 0 when available). Before acting, the menu asks apps with unsaved changes through the existing close path.
- Opened from: the logo menu › Power…, the Control Center power button, the Login screen power button.

## 6. Onboarding — `Welcome`, `OnboardWifi`, `OnboardLook`, `OnboardReady`
- Runs once per user after their first login (`profile.onboarded = false`), or after Sign up, which starts at step 2 for non-first accounts.
- Full-screen wallpaper with a 1000×660 glass card (radius 32, α 80 %, blur 30).
- 4-segment progress bar + "Step N of 4".
- Back / Continue buttons; "Skip for now" where drawn.
- Esc asks "Finish setup later?", which sets onboarded and reaches the desktop.

**Step 1, Welcome:**
- "Hi, \<display name\>. Let's get you settled in." (Display 52)
- Language list: only **English (United States)** is selectable. CareOS has no translations and the fonts are ASCII. The other languages are not shown.
- Region: a list of timezones / regions (drives the existing desktop timezone).
- Keyboard: **US · QWERTY** only, for the same reason.
- "Accessibility options" opens text size and high contrast.

**Step 2, Get online:**
- Wired-only today:
  - shows the Ethernet status card ("Plugged into a cable? CareOS uses Ethernet automatically…" plus the real link/IP)
  - a "Networks nearby" list only when a wireless adapter exists; otherwise "No Wi-Fi adapter found. You're online through Ethernet." (or "offline")
  - Scan / join-network UI uses the phase 1 join-dialog component when wireless exists
- Continue is always enabled.

**Step 3, Pick your vibe:**
- Three large previews: Light / Dark / Auto. Auto = switch at 07:00 / 19:00 local; there's no sunset data, so the copy says "switch morning and evening".
- Accent swatches: Care, Calm, Blue, Green, Purple, Red, Auto.
- Text size: an A/A slider for the UI scale step, if the UI scaling setting exists; otherwise hidden.
- Choices apply live and persist through `kernel/settings.c`.

**Step 4, You're all set:**
- A summary of the real choices (account, network state, theme + accent).
- Three tips: "All your apps live here" (launcher in the dock), "Find anything — Ctrl + Space", "Quick controls — top right".
- "Go to my desktop" (primary) → sets onboarded and plays the existing desktop fade-in.

## Persistence
- New user-profile fields: `avatar_color` (u8), `display_name` (≤32), `password_hint` (≤64), `onboarded` (bool). They're added through the existing versioned user DB in `kernel/users.c`; old records migrate with defaults (onboarded = true for existing users, so current installs don't get onboarding).
- Theme mode (light/dark/auto) and accent are already in settings; `auto` is new.

## Error handling
- Any failed persistence shows an inline error banner (danger-soft card) with what happened and what to try, and never blocks login.
- If onboarding crashes or is interrupted by a reboot, it resumes at the same step (the step is stored in the profile).

## Testing
- **Host tests:**
  - username validation
  - password strength meter bands
  - greeting / step state machine (back/next/skip/resume)
  - user DB migration (old record → defaults)
  - auto theme by hour
- **QEMU QA:**
  - fresh disk → boot splash → first-run flow with all 4 steps → desktop
  - reboot → Login → wrong password (shake + message) → correct
  - Lock via Super+L → unlock
  - Power menu → Restart
  - Sign up of a second account → onboarding from step 2
- Screenshots of every screen in dark (Sign up in light), compared with the artboards.

## Out of scope
Pip (phase 4), PIN creation, real translations, keyboard layouts, ACPI sleep, session restore, Wi-Fi driver.
