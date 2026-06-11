#!/usr/bin/env bash
# Runs inside the archiso chroot during mkarchiso — sets up the live system.
set -euo pipefail

# ── Locale ────────────────────────────────────────────────────────────────────
sed -i 's/#en_US.UTF-8 UTF-8/en_US.UTF-8 UTF-8/' /etc/locale.gen
locale-gen

# ── Timezone (UTC for live, users set their own during install) ───────────────
ln -sf /usr/share/zoneinfo/UTC /etc/localtime

# CareOS TTY banner. This is written after packages are installed so the
# filesystem package cannot move it aside as issue.pacnew.
cat > /etc/issue << 'EOF'
CareOS 2026.05 Plasma \n \l

EOF

# ── Enable systemd services ───────────────────────────────────────────────────
systemctl enable NetworkManager
systemctl enable sddm
systemctl enable bluetooth
systemctl enable cups
systemctl enable power-profiles-daemon
systemctl enable fstrim.timer
systemctl enable systemd-zram-setup@zram0.service

# ── Plymouth ──────────────────────────────────────────────────────────────────
if command -v plymouth-set-default-theme &>/dev/null; then
    plymouth-set-default-theme -R careos || true
fi

# ── CareOS commands — make executable ────────────────────────────────────────
chmod +x /usr/bin/cl
chmod +x /usr/local/bin/careos-update
chmod +x /usr/local/bin/carepkg
chmod +x /usr/local/bin/careos-info
chmod +x /usr/local/bin/careos-help
chmod +x /usr/local/bin/careos-install
chmod +x /usr/local/bin/carectl
chmod +x /usr/local/bin/carescript-studio

# ── Live user ─────────────────────────────────────────────────────────────────
useradd -m -G audio,video,network,storage,optical,wheel,lp,rfkill -s /bin/zsh liveuser
echo "liveuser:liveuser" | chpasswd
echo "root:root"         | chpasswd

# Passwordless sudo for the live session
echo "liveuser ALL=(ALL) NOPASSWD: ALL" > /etc/sudoers.d/liveuser
chmod 440 /etc/sudoers.d/liveuser

# ── Set zsh as root shell too ─────────────────────────────────────────────────
chsh -s /bin/zsh root

# ── SDDM auto-login for live user ────────────────────────────────────────────
mkdir -p /etc/sddm.conf.d
cat > /etc/sddm.conf.d/autologin.conf << 'EOF'
[Autologin]
User=liveuser
Session=plasma
EOF

# ── Copy pre-configured KDE + zsh settings to live user home ─────────────────
cp -rn /etc/skel/. /home/liveuser/
chown -R liveuser:liveuser /home/liveuser/

# Mark desktop shortcuts as trusted
chmod +x /home/liveuser/Desktop/*.desktop 2>/dev/null || true

# Root gets the same config
cp -rn /etc/skel/. /root/
chmod +x /root/Desktop/*.desktop 2>/dev/null || true

# ── XDG user dirs ────────────────────────────────────────────────────────────
su - liveuser -c "xdg-user-dirs-update" 2>/dev/null || true

# ── Register CareOS MIME types ────────────────────────────────────────────────
update-mime-database /usr/share/mime 2>/dev/null || true

# CareOS command aliases available to scripts and muscle memory.
ln -sf /usr/local/bin/careos-install /usr/local/bin/install-careos
ln -sf /usr/local/bin/carectl /usr/local/bin/careos-control

# ── Update icon cache ─────────────────────────────────────────────────────────
gtk-update-icon-cache -f /usr/share/icons/Papirus-Dark 2>/dev/null || true
gtk-update-icon-cache -f /usr/share/icons/hicolor 2>/dev/null || true

# ── Set Kvantum theme symlink so it's available system-wide ──────────────────
mkdir -p /usr/share/Kvantum
ln -sf /usr/share/Kvantum/CareOS /usr/share/Kvantum/kvantum-dark 2>/dev/null || true
