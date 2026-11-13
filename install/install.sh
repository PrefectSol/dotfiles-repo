#!/bin/bash
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
repo=$(dirname "$here")
source "$here/lib.sh"

conf=${1:-}
[[ -f $conf ]] || die "usage: install.sh CONFIG"
source "$conf"
for v in DISK HOST USERNAME PASSWORD TIMEZONE SWAP_SIZE MIRROR_COUNTRIES; do
	[[ -n ${!v:-} ]] || die "$v is not set in $conf"
done
[[ -d /sys/firmware/efi ]] || die "UEFI boot required"
[[ -b $DISK ]] || die "$DISK is not a block device"

part() { [[ $DISK =~ [0-9]$ ]] && echo "${DISK}p$1" || echo "${DISK}$1"; }

log "preparing live environment"
timedatectl set-ntp true
for _ in $(seq 60); do systemctl is-active -q pacman-init.service && break; sleep 1; done
tune_pacman /etc/pacman.conf
systemctl stop reflector.service 2>/dev/null || true
log "ranking mirrors ($MIRROR_COUNTRIES)"
reflector --country "$MIRROR_COUNTRIES" --protocol https --age 24 --latest 30 \
	--sort rate --number 10 --save /etc/pacman.d/mirrorlist || log "reflector failed, keeping default mirrorlist"
pacman -Sy --noconfirm --needed archlinux-keyring

log "partitioning $DISK"
umount -R /mnt 2>/dev/null || true
swapoff -a || true
wipefs -af "$DISK"
sgdisk -Z "$DISK"
sgdisk -n1:0:+512M -t1:ef00 -c1:EFI -n2:0:0 -t2:8304 -c2:root "$DISK"
partprobe "$DISK"
udevadm settle
mkfs.fat -F32 -n EFI "$(part 1)"
mkfs.ext4 -qF -L root "$(part 2)"
mount "$(part 2)" /mnt
mount --mkdir -o fmask=0077,dmask=0077 "$(part 1)" /mnt/efi

pkgs=$(grep -vE '^\s*(#|$)' "$here/packages.txt")
if [[ ${VM:-0} != 1 ]]; then
	pkgs+=" linux-firmware"
	grep -q GenuineIntel /proc/cpuinfo && pkgs+=" intel-ucode"
	grep -q AuthenticAMD /proc/cpuinfo && pkgs+=" amd-ucode"
fi

if [[ -f $repo/out/repo/$REPO_NAME.db ]]; then
	add_local_repo /etc/pacman.conf "$repo/out/repo"
	pacman -Sy
	pkgs+=" $(grep -vE '^\s*(#|$)' "$here/packages-local.txt")"
fi

log "pacstrap"
pacstrap -K /mnt $pkgs
genfstab -U /mnt >> /mnt/etc/fstab

log "system overlay"
install_overlay "$repo/system" /mnt
cp /etc/pacman.d/mirrorlist /mnt/etc/pacman.d/mirrorlist

if [[ ${VM:-0} == 1 ]]; then
	install -dm700 /mnt/root/.ssh
	install -m600 "$SSH_KEY_FILE" /mnt/root/.ssh/authorized_keys
	mkdir -p /mnt/repo
	[[ -f $repo/out/repo/$REPO_NAME.db ]] && add_local_repo /mnt/etc/pacman.conf /repo/out/repo
	echo 'repo /repo 9p trans=virtio,version=9p2000.L,nofail,x-systemd.automount 0 0' >> /mnt/etc/fstab
fi

trap 'rm -rf /mnt/root/install' EXIT
install -dm700 /mnt/root/install
install -m600 "$conf" /mnt/root/install/install.conf
install -m755 "$here/chroot.sh" "$here/lib.sh" "$here/grub-theme.sh" /mnt/root/install/
cp -r "$repo/theme/grub" /mnt/root/install/grub-theme

log "chroot"
arch-chroot /mnt /root/install/chroot.sh
rm -rf /mnt/root/install
log "done, reboot when ready"
