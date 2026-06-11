#!/bin/sh
# CareOS environment profile.

export CAREOS_NAME="CareOS"
export CAREOS_VERSION="2026.05"
export CAREOS_CODENAME="kindred"
export CAREOS_BASE="Arch Linux"
export CAREOS_EDITION="Plasma"

export CAREOS_COLOR_PRIMARY="#559aff"
export CAREOS_COLOR_ACCENT="#82bcff"
export CAREOS_COLOR_BG="#080d17"
export CAREOS_COLOR_SUCCESS="#2ecc8e"
export CAREOS_COLOR_ALERT="#f56060"

export PATH="$PATH:/usr/local/bin"

export EDITOR=kate
export VISUAL=kate
export TERMINAL=konsole
export BROWSER=firefox

alias care='carectl'
alias sys='carectl status'
alias doctor='carectl doctor'
alias pkg='carepkg'
alias update='careos-update'
alias install-careos='careos-install'
