#!/bin/bash
set -euo pipefail
src=${1:?usage: grub-theme.sh THEME_DIR}
t=/usr/share/grub/themes/distro
rm -rf "$t" /usr/share/grub/themes/vimix
install -dm755 "$t"
install -m644 "$src"/*.png "$src/theme.txt" "$t"
read -r ttf size < "$src/font"
grub-mkfont -n Mono -s "$size" -r 0x20-0x7e,0x400-0x45f -o "$t/mono.pf2" "/usr/share/fonts/TTF/$ttf" 2>/dev/null
chmod 644 "$t/mono.pf2"
