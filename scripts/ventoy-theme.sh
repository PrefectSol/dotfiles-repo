#!/bin/bash
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
mnt=${1:?usage: ventoy-theme.sh VENTOY_MOUNT}
[[ -d $mnt ]] || { echo "not a directory: $mnt" >&2; exit 1; }
grub=$repo/theme/grub
dst=$mnt/ventoy/theme/distro
cfg=$mnt/ventoy/ventoy.json
dim=#$(awk '$1 == 8 { print $2 }' "$repo/theme/palette")

alias_of() {
	local b=${1##*/}
	b=${b%.*}
	b=${b,,}
	case $b in
	archlinux*) echo "arch linux" ;;
	win11*) echo "windows 11" ;;
	win10*) echo "windows 10" ;;
	*) echo "${b//[_-]/ }" ;;
	esac
}

mapfile -t images < <(cd "$mnt" && find . -path ./ventoy -prune -o -type f \
	\( -iname '*.iso' -o -iname '*.img' -o -iname '*.wim' -o -iname '*.vhd' -o -iname '*.vhdx' -o -iname '*.efi' -o -iname '*.vtoy' \) \
	-printf '/%P\n' | sort -V)

read -r step frame top gap label min max < "$grub/layout"
n=${#images[@]}
((n < min)) && n=$min
((n > max)) && n=$max
h=$((n * step + frame))

mkdir -p "$dst"
cp "$grub"/sel_*.png "$dst"
cp "$grub/frame-$n.png" "$dst/frame.png"
sed -e "s/@HALF@/$((h / 2))/" -e "s/@BOX_H@/$h/" -e "s/@MENU_TOP@/$((h / 2 - top))/" \
	-e "s/@MENU_H@/$((n * step - gap))/" -e "s/@LABEL_TOP@/$((h / 2 + label))/" \
	"$repo/theme/ventoy/theme.txt" > "$dst/theme.txt"
read -r ttf size < "$grub/font"
grub-mkfont -n Mono -s "$size" -r 0x20-0x7e,0x400-0x45f -o "$dst/mono.pf2" "/usr/share/fonts/TTF/$ttf" 2>/dev/null

aliases=$(for i in "${images[@]}"; do jq -n --arg i "$i" --arg a "$(alias_of "$i")" '{image: $i, alias: $a}'; done | jq -s .)
theme=$(jq -n --arg dim "$dim" '{
	file: "/ventoy/theme/distro/theme.txt",
	gfxmode: "2560x1440,1920x1080,auto",
	fonts: ["/ventoy/theme/distro/mono.pf2"],
	ventoy_left: "38",
	ventoy_top: "100%-72",
	ventoy_color: $dim
}')
{ [[ -s $cfg ]] && cat "$cfg" || echo '{}'; } |
	jq --argjson t "$theme" --argjson a "$aliases" '.theme = $t | .menu_alias = $a' > "$cfg.new"
mv "$cfg.new" "$cfg"
sync
echo "theme for ${#images[@]} images installed to $mnt"
