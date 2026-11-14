#!/bin/bash
set -euo pipefail
repo=$(cd "$(dirname "$0")" && pwd)
source "$repo/install/lib.sh"

(($# == 0)) || die "usage: ./setup.sh"

[[ -f /etc/arch-release ]] || die "Arch Linux required"
((EUID == 0)) || exec sudo "$0" "$@"
user=${SUDO_USER:-}
[[ -n $user && $user != root ]] || die "run it as your regular user: ./setup.sh"
home=$(getent passwd "$user" | cut -d: -f6)
group=$(id -gn "$user")

echo "$user ALL=(ALL) NOPASSWD: ALL" > /etc/sudoers.d/99-setup
trap 'rm -f /etc/sudoers.d/99-setup' EXIT

grub=0
pacman -Q grub &>/dev/null && [[ -f /boot/grub/grub.cfg ]] && grub=1

log "pacman"
tune_pacman /etc/pacman.conf

log "system overlay"
cmdline=$(grep -s '^GRUB_CMDLINE_LINUX=' /etc/default/grub || true)
if ((grub)); then
	install_overlay "$repo/system" /
	if [[ -n $cmdline ]]; then
		sed -i '/^GRUB_CMDLINE_LINUX=/d' /etc/default/grub
		echo "$cmdline" >> /etc/default/grub
	fi
else
	install_overlay "$repo/system" / '^etc/(default/grub|grub\.d/)'
fi

log "packages"
skip='^$'
((grub)) || skip='^(grub|efibootmgr|os-prober)$'
pacman -Qq linux-lts linux-zen linux-hardened 2>/dev/null | grep -q . && skip+='|^linux$'
retry pacman -Syu --noconfirm --needed $(grep -vE '^\s*(#|$)' "$repo/install/packages.txt" | grep -vE "$skip")

log "local packages"
pacman -Qq vimix-cursors &>/dev/null || rm -rf /usr/share/icons/Vimix-cursors
out=/srv/$REPO_NAME
install -d -o "$user" -g "$group" -m755 "$out"
if [[ ! -f $out/$REPO_NAME.db ]]; then
	tar -czf "$out/$REPO_NAME.db.tar.gz" -T /dev/null
	ln -sf "$REPO_NAME.db.tar.gz" "$out/$REPO_NAME.db"
	chown -h "$user:$group" "$out/$REPO_NAME.db" "$out/$REPO_NAME.db.tar.gz"
fi
add_local_repo /etc/pacman.conf "$out"
pacman -Sy
retry sudo -u "$user" DISTRO_REPO="$out" "$repo/scripts/build-repo.sh"
retry pacman -Su --noconfirm --needed $(grep -vE '^\s*(#|$)' "$repo/install/packages-local.txt")

log "yay"
install_yay "$user"

log "nvidia"
install_nvidia "$user"

log "locales"
gen_locales

log "initramfs"
mkinitcpio -P

log "bootloader"
if ((grub)); then
	"$repo/install/grub-theme.sh" "$repo/theme/grub"
	grub-mkconfig -o /boot/grub/grub.cfg
	"$repo/install/grub-theme.sh" "$repo/theme/grub"
else
	for e in /boot/loader/entries/*.conf /efi/loader/entries/*.conf /boot/efi/loader/entries/*.conf; do
		[[ -f $e ]] || continue
		grep -q fbcon=nodefer "$e" || sed -i -E '/^options/ s/$/ fbcon=nodefer/' "$e"
	done
fi

log "miniforge"
install_miniforge "$user"

log "services"
enable_services

log "user $user"
chsh -s /usr/bin/zsh "$user" >/dev/null
sudo -u "$user" mkdir -p "$home/.config"
for d in nvim yazi; do
	[[ -e $home/.config/$d ]] || sudo -u "$user" cp -r "/etc/skel/.config/$d" "$home/.config/$d"
done
[[ -e $home/.config/user-dirs.dirs ]] || sudo -u "$user" cp /etc/skel/.config/user-dirs.dirs "$home/.config/user-dirs.dirs"
sudo -u "$user" mkdir -p "$home/downloads"
[[ -e $home/.zshrc ]] || sudo -u "$user" touch "$home/.zshrc"

log "done, reboot to start the desktop"
