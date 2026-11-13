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
YAY_COMMIT=a2812a593d4017a657bbedf98761db78072b5e4c
MINIFORGE_VERSION=26.9.4-2

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

retry() {
	local i
	for i in 1 2 3; do
		"$@" && return
		log "failed, retry $i/3: $*"
		sleep 5
	done
	return 1
}

install_yay() {
	local user=$1 tmp
	command -v yay >/dev/null && return
	tmp=$(sudo -u "$user" mktemp -d)
	retry sudo -u "$user" bash -c "rm -rf $tmp/yay && git clone -q https://aur.archlinux.org/yay-bin.git $tmp/yay && cd $tmp/yay && git checkout -q $YAY_COMMIT && makepkg -si --noconfirm"
	rm -rf "$tmp"
}

install_miniforge() {
	local user=$1
	[[ -d /opt/conda ]] && return
	local url=https://github.com/conda-forge/miniforge/releases/download/$MINIFORGE_VERSION/Miniforge3-$MINIFORGE_VERSION-Linux-x86_64.sh
	retry curl -fsSL -o /tmp/miniforge.sh "$url"
	retry curl -fsSL -o /tmp/miniforge.sha256 "$url.sha256"
	[[ $(sha256sum /tmp/miniforge.sh | cut -d' ' -f1) == $(cut -d' ' -f1 /tmp/miniforge.sha256) ]] || die "miniforge checksum mismatch"
	bash /tmp/miniforge.sh -b -p /opt/conda
	rm -f /tmp/miniforge.sh /tmp/miniforge.sha256
	printf 'auto_activate_base: false\nchangeps1: false\n' > /opt/conda/.condarc
	chown -R "$user:$user" /opt/conda
}

enable_services() {
	systemctl enable NetworkManager systemd-timesyncd fstrim.timer reflector.timer paccache.timer vtrgb earlyoom
	systemctl --global enable ssh-agent.socket
	if pacman -Q lidm &>/dev/null; then
		systemctl disable display-manager.service 2>/dev/null || true
		systemctl disable getty@tty1
		systemctl enable lidm
	fi
}
