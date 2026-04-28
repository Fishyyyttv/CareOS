# CareOS (Full Update)

CareOS is a 32-bit (currently being migrated to 64-bit) x86 desktop operating system project written in C and x86 assembly.

## Highlights in this build

- Persistent login accounts with stronger password policy and lockout handling.
- Persistent system settings (theme, mouse sensitivity, boot fast, clock format, wallpaper, Wi-Fi profile).
- Persistent `/home` filesystem snapshot on disk image (when ATA disk is available).
- Improved boot order so ATA-backed settings/users/home data initialize correctly.
- Upgraded network stack:
  - DHCP renew helper
  - Configurable DNS server
  - Runtime DNS query path with fallback map
- New shell commands:
  - `wifi scan|status|connect|disconnect`
  - `settings show|set ...`
  - `dmesg [clear]`
- GUI polish:
  - Optional fast boot splash (from settings)
  - Alt+Tab window cycling
  - Ctrl+Alt+H/J/K/L window snap, Ctrl+Alt+M maximize

## Default accounts

- `user` / `CareOS123`
- `root` / `root`

## Build

```bash
make clean
make
```

## Run (QEMU)

```bash
make run
```

## Headless serial boot

```bash
make run-nowindow
```

## Persistence notes

- `make clean` keeps `careos.img` so users/settings/home data stay.
- `make reset-disk` creates a fresh blank disk image.
- If no ATA disk is detected in the current QEMU mode, persistence falls back to in-memory.
"# CareOS" 
