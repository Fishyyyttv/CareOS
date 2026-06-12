#!/usr/bin/env python3
"""Generate CareOS system sounds. Run once on dev machine, commit the .ogg output."""
import numpy as np
from scipy.io import wavfile
import subprocess, os, tempfile
from pathlib import Path

SR = 44100
OUT = Path("iso/airootfs/usr/share/sounds/CareOS/stereo")
OUT.mkdir(parents=True, exist_ok=True)

def sine(freq, dur, amp=0.4):
    t = np.linspace(0, dur, int(SR * dur), False)
    return (amp * np.sin(2 * np.pi * freq * t)).astype(np.float32)

def fade(sig, fade_s=0.05):
    n = int(SR * fade_s)
    sig[:n]  *= np.linspace(0, 1, n)
    sig[-n:] *= np.linspace(1, 0, n)
    return sig

def chord(freqs, dur, amp=0.25):
    return sum(fade(sine(f, dur, amp)) for f in freqs)

def to_ogg(sig, name):
    wav = OUT / f"{name}.wav"
    ogg = OUT / f"{name}.ogg"
    wavfile.write(str(wav), SR, (sig * 32767).astype(np.int16))
    subprocess.run(["ffmpeg", "-y", "-i", str(wav), str(ogg)], check=True,
                   capture_output=True)
    wav.unlink()
    print(f"  wrote {ogg}")

# startup — ascending soft chord C4 E4 G4
startup = np.concatenate([
    chord([261.6, 329.6], 0.4),
    chord([261.6, 329.6, 392.0], 1.0),
])
to_ogg(startup, "startup")

# notification — two-note chime E5 A5
notif = np.concatenate([
    fade(sine(659.3, 0.18, 0.35)),
    np.zeros(int(SR * 0.04)),
    fade(sine(880.0, 0.28, 0.3)),
])
to_ogg(notif, "notification")

# error — descending A4 E4
err = np.concatenate([
    fade(sine(440.0, 0.2, 0.3)),
    np.zeros(int(SR * 0.03)),
    fade(sine(329.6, 0.3, 0.25)),
])
to_ogg(err, "error")

# logout — soft descending G4 E4 C4
logout = np.concatenate([
    chord([392.0, 329.6], 0.35),
    chord([329.6, 261.6], 0.35),
    chord([261.6], 0.6),
])
to_ogg(logout, "logout")

print("Done.")
