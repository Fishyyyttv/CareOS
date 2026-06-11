#!/usr/bin/env bash
# shellcheck disable=SC2034

iso_name="CareOS"
iso_label="CAREOS_$(date --date="@${SOURCE_DATE_EPOCH:-$(date +%s)}" +%Y%m)"
iso_publisher="CareOS Project <careos.local>"
iso_application="CareOS Live Desktop"
iso_version="$(date --date="@${SOURCE_DATE_EPOCH:-$(date +%s)}" +%Y.%m.%d)"
install_dir="careos"
buildmodes=('iso')
bootmodes=(
    'uefi.grub'
)
arch="x86_64"
pacman_conf="pacman.conf"
airootfs_image_type="squashfs"
airootfs_image_tool_options=('-comp' 'zstd' '-b' '1M' '-Xcompression-level' '15')

file_permissions=(
    ["/root"]="0:0:750"
    ["/root/customize_airootfs.sh"]="0:0:755"
    ["/usr/bin/cl"]="0:0:755"
    ["/usr/local/bin/careos-update"]="0:0:755"
    ["/usr/local/bin/carepkg"]="0:0:755"
    ["/usr/local/bin/careos-info"]="0:0:755"
    ["/usr/local/bin/careos-help"]="0:0:755"
    ["/usr/local/bin/careos-install"]="0:0:755"
    ["/usr/local/bin/carectl"]="0:0:755"
    ["/etc/profile.d/careos.sh"]="0:0:644"
)
