# CareOS KDE Plasma Linux Distro Design

**Date:** 2026-05-05
**Goal:** Convert CareOS from a bare-metal x86 OS into an Arch Linux-based desktop distribution shipping KDE Plasma, with a CareOS-first identity across boot, login, desktop, installer, shell, package commands, and welcome flow.

## Decision Summary
- Base: Arch Linux (archiso build system)
- Desktop: KDE Plasma (plasma-meta) with CareOS Shell defaults
- Installer: `careos-install` launcher using a CareOS-branded install flow; Calamares branding is staged for future graphical builds
- Output: Bootable x86_64 ISO replacing careos.iso
- Build environment: WSL2 Arch Linux
- Carry-over from old CareOS: branding, colors, CL scripting, and CareOS command identity

## Color Palette
- Background:  #080d17
- Surface:     #0f1726
- Primary:     #559aff
- Accent:      #82bcff
- Text:        #eaf0ff
- Dim:         #8a99ba
- Border:      #2c3c58
- Success:     #2ecc8e
- Error:       #f56060

## Architecture
```text
iso/                        archiso build profile
  profiledef.sh             ISO metadata + build options
  packages.x86_64           package list (Plasma, drivers, fonts, tools)
  pacman.conf               repos (core, extra, multilib)
  build.sh                  one-command build wrapper
  grub/grub.cfg             CareOS live ISO GRUB menu
  airootfs/                 overlay onto live filesystem
    etc/
      hostname              -> careos
      os-release            -> CareOS identity
      careos-release        -> CareOS release metadata
      sddm.conf.d/          -> CareOS SDDM theme
      calamares/            -> staged graphical installer branding
    usr/local/bin/
      carectl               -> CareOS control command
      carepkg               -> CareOS package command
      careos-install        -> CareOS installer launcher
      careos-update         -> CareOS updater
    usr/share/
      wallpapers/CareOS/    -> CareOS wallpaper
      color-schemes/        -> CareOS.colors KDE color scheme
      sddm/themes/careos/   -> QML login screen
      careos/welcome/       -> CareOS welcome center
    etc/skel/.config/       -> CareOS KDE pre-configuration for new users
    root/customize_airootfs.sh -> runs in chroot, enables services + live user
```

## What the user needs to install (WSL2 Arch)
1. `wsl --install -d Arch` (or install "Arch Linux" from Microsoft Store)
2. In Arch WSL: `sudo pacman -Syu && sudo pacman -S archiso git`
3. Then: `cd /path/to/CareOS_v9 && sudo bash iso/build.sh`
