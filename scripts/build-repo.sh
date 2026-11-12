#!/bin/bash
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
source "$repo/install/lib.sh"
out=${DISTRO_REPO:-$repo/out/repo}
db=$out/$REPO_NAME.db.tar.gz
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT
mkdir -p "$out"

pkgs=()
for d in "$repo"/pkgs/*/; do
	p=$(basename "$d")
	[[ -f $d/PKGBUILD ]] || continue
	(($#)) && [[ " $* " != *" $p "* ]] && continue
	k=0
	grep -q '^arch=(any)' "$d/PKGBUILD" && k=1
	[[ $p == distro-desktop ]] && k=2
	pkgs+=("$k $p")
done

for p in $(printf '%s\n' "${pkgs[@]}" | sort | cut -d' ' -f2); do
	echo "==> $p"
	(cd "$repo/pkgs/$p" && BUILDDIR=$build PKGDEST=$out SRCDEST=$build makepkg -fcs --noconfirm --needed)
	mapfile -t files < <(cd "$repo/pkgs/$p" && PKGDEST=$out makepkg --packagelist)
	repo-add -q -n -R "$db" "${files[@]}"
	sudo pacman -Sy >/dev/null
done
