# Phase 4 — Pip, the desktop pet

**Date:** 2026-10-04 · Part of the v10 roadmap (`2026-10-03-careos-v10-redesign-roadmap.md`)
**Reference artboards** (`docs/design/careos-v10/`): `Pet` (the character), `PetMoods`, `PetHome`, `PetDesktop`; cameo slots in `Lock` and `Power`.

## Goal

Pip is a small creature who lives on the CareOS desktop. He reacts to what really happens on the computer, can be looked after and dressed up, and has a home app. He looks like the Pet artboard at every size and in every outfit.

## Decisions (brainstorming, 2026-10-04)

| Topic | Decision |
|---|---|
| Stats | **A real pet model.** Happiness, energy and friendship come from a small saved simulation (below). Nothing is invented. |
| Default spot | **Perched by the dock.** Pip roams now and then, or when asked; napping is on the dock. |
| Rendering | **A runtime vector renderer plus a sprite cache.** Pip is described as shapes on the artboard's 160-unit grid, drawn by a small anti-aliased rasterizer, and cached per frame and size. Any combination of looks is free. |
| Speech | **Canned lines tied to real events and real system state only.** No fake intelligence; that's for the future AI assistant. |
| Scripts | Care scripts can drive Pip: `pip_mood("happy")` and `pip_say("hi!")`. |

## 1. Units

| File | Role | Pure / tested |
|---|---|---|
| `gui/pip/pip_raster.[ch]` | Shape primitives on an ARGB canvas: filled and rotated ellipses, circles, round-cap strokes along quadratic curves, quadratic-path fills (even-odd), linear and radial gradient fills, per-shape opacity. 4×4 supersampled coverage, integer/Q16 maths only (kernel builds use `-mno-sse`). | yes |
| `gui/pip/pip_art.[ch]` | Pip as data: `pip_look_t` (colour, hat, outfit, extra, effect) + `pip_pose_t` (mood, animation phase 0..65535, look offset) → an ordered shape list, transcribed from the Pet artboard. | yes |
| `gui/pip/pip_sprite.[ch]` | Renders and caches frames: key (look, mood, frame index, size). An LRU of ARGB sprites, about 2 MB budget; a look change clears it. | no |
| `gui/pip/pip_brain.[ch]` | The pet model: stats, the mood stack, catch-up after time away, and save/load of the `~/.pip` text. | yes |
| `gui/pip/pip.[ch]` | Runtime glue: owns the brain and the sprites, takes events (`pip_event(...)`), ticks every frame, gives the cameo API. | no |
| `gui/pip/pip_desktop.c` | Perch / roam / nap on the desktop, bubbles, status pill, context menu, hit-testing. | no |
| `apps/app_pip.c` | The Pip's Home app (`APP_PIP`, in the launcher and dock as "Pip"). | no |

## 2. The character (from `Pet`)

- **Colours** (base, light, dark, belly):
  - lilac `#9C8CFF #CEC5FF #6A57DE #EEEAFF`
  - coral `#FF8A6B #FFC0AD #E0573A #FFE7DF`
  - mint `#5FD9A8 #A8F0D1 #25A574 #E4FAF0`
  - sky `#5CC0F2 #A8E0FB #2786C4 #E2F4FD`
  - honey `#FFC24B #FFE09A #DD9112 #FFF4D6`
  - bubblegum `#FF8EC4 #FFC4E1 #DD4F94 #FFE8F3`
  - midnight `#3E4A78 #6370A6 #222A4A #8B96C4` (ink `#F4F1FF`, shine `#1A1630`)
  - snow `#ECEFF8 #FFFFFF #B8BFD6 #FFFFFF`
  - The ink is `#1A1630` and the shine `#FFFFFF`, except on midnight.
- **Layer order** (back to front): glow effect, rainbow effect, play streaks, shadow, cape, ears, feet, arms (the right arm waves), body (radial gradient light→base→dark, a 2-unit dark outline at 28 %), belly, highlights, cheeks, outfit (scarf, bow tie, hoodie, overalls), antenna + bulb halo + bulb, eyes, mouth, extras (glasses, shades, star clip), hat (beanie, crown, party, wizard, cap, bow, headphones, sprout), mood overlays (tear, think dots, ?, z, !), sparkles / hearts / bubbles.
- The antenna shows only with no hat, the bow or the headphones.
- **Moods** (eyes, mouth, body animation, bulb):
  - idle: open, smile, bob 2.6 s, `#8C9BFF`, blinks every 4.2 s
  - happy: happy, big, bounce 0.95 s, `#FF8EC4`, waves, hearts
  - sad: sad, frown, droop 3.2 s, `#6CB8FF`, tear, blush 0.15
  - thinking: open looking up-right, flat, tilt 3 s, `#FFC24B`, the bulb pulses, think dots
  - confused: confused, wavy, wobble 1.8 s, `#C08BFF`, the bulb pulses, "?"
  - watching: open, the eyes follow the cursor, o, bob 3.2 s, `#3DD68C`
  - playing: happy, big, hop 0.5 s, `#FF7A59`, the feet step, play streaks
  - sleeping: closed, o, breathe 3.4 s, `#5A6385`, "z"
  - excited: star, big, jump 0.85 s, `#FFC24B`, waves, "!"
- The CSS keyframes (bob, bounce, droop, tilt, wobble, hop, breathe, jump, and the matching shadow scales) become integer keyframe tables of (dy, sx, sy, rotation) applied to the body group about (80, 144). A mood change plays the artboard's 420 ms squash.
- **Animation:** 12 frames a second. One cycle of each mood is cached as 8–16 frames. The blink, the wave and the look-around are extra frames.

## 3. The pet model (`pip_brain`)

- **State:** name (14 chars, default "Pip"), look, happiness 0..100 (start 70), energy 0..100 (start 80), friendship points (level = 1 + points/40, max 10), birthday (RTC date at the first sign-in), last-seen time, days-together count, pats this hour, toggles (roam, follow cursor, nap), mode.
- **Ticks:** each minute of wall-clock time while signed in.
  - Happiness falls by 1 every 30 min.
  - Energy falls by 1 every 2 min while roaming or playing.
  - Energy rises by 2 a minute while napping or tucked in.
- **Catch-up at sign-in:** up to 3 days of time away. Energy is restored (sleep), and happiness falls but **not below 30** ("missed you", never miserable).
- **Pats:** +8 happiness each, at most +24 an hour; +1 friendship point; always lift a sad Pip to happy. A new day of signing in gives +5 friendship.
- **Mood:** an event mood (4–8 s) beats the resting mood.
  - Events:
    - pat → happy
    - run passed or back after 1 h or more → excited
    - run failed → sad
    - unknown command or error notification → confused
    - an app or page loading → thinking
  - Resting, first match wins:
    - sleeping: energy < 15, or 10 idle minutes with nap on, or 23:00–07:00 with no input for 2 min
    - sad: happiness < 25
    - playing: roaming
    - watching: follow on and the cursor within 200 px
    - else idle
- **Saving:** `~/.pip`, versioned `key=value` text, written on change (at most once every 10 s) and at sign-out. A damaged or missing file starts fresh. Guests get an unsaved Pip; the home app says so.

## 4. On the desktop (`PetDesktop`)

- **Modes:**
  - **Perched** (default): 120 px, seated on the right of the dock.
  - **Roaming**: walks the dock's top edge and hops onto visible window tops with the phase 1 spring; returns after about 40 s, or when energy < 30.
  - **Napping**: 110 px, lying on the dock.
- **Input:**
  - Click → pat.
  - Right-click → menu: Give a pat, Ask what he's thinking, Go play / Come back, Dress up….
  - While he's out, a small status pill shows his name, status and mode buttons (Roam / Perch / Nap) and a Send home link. It appears while the pointer is over him.
- **Bubbles:** short lines tied to the trigger, e.g. "Build passed!!", "Hmm, that didn't run.", "zzz…", "Hehe, that tickles!".
  - "Ask what he's thinking" reports something true: open windows, uptime, his energy, the time.
- **Layering:** above windows, below the menu bar, Control Center, notifications and launcher. Hidden in fullscreen apps (DOOM, 3D).
- **Performance:** only Pip's rectangle (sprite plus bubble) is damaged per frame. Roaming must keep the VBox benchmark at the phase 1 bar (≥ 30 fps, p95 ≤ 33 ms).

## 5. Pip's Home (`PetHome`)

- **Room panel:** a sky-gradient room with floor and window, a small Pip portrait, "<name>, age N days", and the big live Pip at 280 px.
- **Panel controls:** name field (14 chars), meters (Happiness %, Energy %, Friendship Lv), the mood row (9 chips that play a mood for a few seconds), and Pat / Tuck in (starts a nap) / Surprise me (a random look).
- **"Let <name> out":** switches to roaming and minimises the app.
- **Dress-up aside:** tabs Color / Hats / Outfits / Extras / Effects, with option tiles showing Pip in each choice (64 px still sprites). Changes apply everywhere at once.
- **"When <name> is out" toggles:** Roam around, Follow my cursor, Nap when I'm away.

## 6. Cameos and scripts

- **Lock screen:** `session_pet_slot` draws Pip asleep (or excited for a moment on unlock).
- **Power menu:** Pip looks worried (confused) above "Taking a break?".
- **Care natives:** `pip_mood(name)` and `pip_say(text)` (shown as a bubble, 60 chars). The Editor docs panel gets the "Talk to Pip" card.

## 7. Real data or hide

Pip's stats, age and bubbles come from the model and the system. The PetDesktop artboard's sample terminal and editor content isn't reproduced. Missing parts degrade quietly. With no saved file, Pip is new.

## 8. Testing

- **Host:**
  - `pip_raster`: coverage of known circles / ellipses / curve strokes, alpha blending, clipping at the canvas edge.
  - `pip_art`: every look × mood builds a shape list inside bounds, and the antenna rule holds.
  - `pip_brain`: decay, the happiness floor after time away, energy in nap vs roam, the pat cap, the mood priority stack, the sleep rules, the level maths, and save/load round-trip plus a damaged file.
- **QEMU QA:**
  - a mood sheet compared with `PetMoods`
  - Pip's Home compared with `PetHome`
  - desktop perch / roam / nap compared with `PetDesktop`
  - lock and power cameos
  - Care `pip_say`
- **VBox:** the benchmark with Pip roaming.

## Out of scope

Pip notifications, multiple pets, syncing between users, AI conversation, sound.
