#!/bin/bash
set -euo pipefail
src=${1:?usage: grub-theme.sh THEME_DIR}
t=/usr/share/grub/themes/distro
read -r step frame top gap label min max < "$src/layout"
n=0
[[ -f /boot/grub/grub.cfg ]] && n=$(awk '/^[ \t]*(menuentry|submenu) / { if (!d) n++; if ($0 !~ /}[ \t]*$/) d++; next } /^[ \t]*}[ \t]*$/ { if (d) d-- } END { print n + 0 }' /boot/grub/grub.cfg)
((n < min)) && n=$min
((n > max)) && n=$max
h=$((n * step + frame))
rm -rf "$t" /usr/share/grub/themes/vimix
install -dm755 "$t"
install -m644 "$src"/sel_*.png "$t"
install -m644 "$src/frame-$n.png" "$t/frame.png"
sed -e "s/@HALF@/$((h / 2))/" -e "s/@BOX_H@/$h/" -e "s/@MENU_TOP@/$((h / 2 - top))/" \
	-e "s/@MENU_H@/$((n * step - gap))/" -e "s/@LABEL_TOP@/$((h / 2 + label))/" "$src/theme.txt" > "$t/theme.txt"
chmod 644 "$t/theme.txt"
read -r ttf size < "$src/font"
grub-mkfont -n Mono -s "$size" -r 0x20-0x7e,0x400-0x45f -o "$t/mono.pf2" "/usr/share/fonts/TTF/$ttf" 2>/dev/null
chmod 644 "$t/mono.pf2"
