log() { printf '\e[1;36m==> %s\e[0m\n' "$*"; }
die() { printf '\e[1;31m!! %s\e[0m\n' "$*" >&2; exit 1; }

tune_pacman() {
	local f=${1:-/etc/pacman.conf}
	sed -i -E \
		-e 's/^#?Color$/Color/' \
		-e 's/^#?VerbosePkgLists$/VerbosePkgLists/' \
		-e 's/^#?ParallelDownloads.*/ParallelDownloads = 5/' "$f"
	grep -q '^ILoveCandy' "$f" || sed -i '/^ParallelDownloads/a ILoveCandy' "$f"
}

REPO_NAME=distro

add_local_repo() {
	local f=$1 dir=$2
	grep -q "^\[$REPO_NAME\]" "$f" && return
	sed -i "/^\[core\]/i [$REPO_NAME]\nSigLevel = Optional TrustAll\nServer = file://$dir\n" "$f"
}

install_overlay() {
	local src=$1 dst=$2 skip=${3:-^$} f
	(cd "$src" && find . -type f | sed 's|^\./||') | grep -vE "$skip" | while read -r f; do
		install -Dm644 "$src/$f" "$dst/$f"
	done
	[[ -f $dst/etc/grub.d/41_power ]] && chmod 755 "$dst/etc/grub.d/41_power"
	chmod 440 "$dst/etc/sudoers.d/10-wheel"
}

gen_locales() {
	sed -i -E 's/^#(en_US\.UTF-8|ru_RU\.UTF-8) UTF-8/\1 UTF-8/' /etc/locale.gen
	locale-gen
}

install_cursor() {
	local tmp
	tmp=$(mktemp -d)
	git clone -q --depth 1 https://github.com/vinceliuice/vimix-cursors.git "$tmp/cursors"
	rm -rf /usr/share/icons/Vimix-cursors
	cp -r "$tmp/cursors/dist" /usr/share/icons/Vimix-cursors
	rm -rf "$tmp"
}

install_yay() {
	local user=$1 tmp
	command -v yay >/dev/null && return
	tmp=$(sudo -u "$user" mktemp -d)
	sudo -u "$user" bash -c "git clone -q https://aur.archlinux.org/yay-bin.git $tmp/yay && cd $tmp/yay && makepkg -si --noconfirm"
	rm -rf "$tmp"
}

install_miniforge() {
	local user=$1
	[[ -d /opt/conda ]] && return
	curl -fsSL -o /tmp/miniforge.sh https://github.com/conda-forge/miniforge/releases/latest/download/Miniforge3-Linux-x86_64.sh
	bash /tmp/miniforge.sh -b -p /opt/conda
	rm -f /tmp/miniforge.sh
	printf 'auto_activate_base: false\nchangeps1: false\n' > /opt/conda/.condarc
	chown -R "$user:$user" /opt/conda
}

enable_services() {
	systemctl enable NetworkManager systemd-timesyncd fstrim.timer reflector.timer paccache.timer vtrgb
	if pacman -Q lidm &>/dev/null; then
		systemctl disable display-manager.service 2>/dev/null || true
		systemctl disable getty@tty1
		systemctl enable lidm
	fi
}
