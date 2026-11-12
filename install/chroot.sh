#!/bin/bash
set -euo pipefail
source /root/install/lib.sh
source /root/install/install.conf

log "time and locale"
ln -sf "/usr/share/zoneinfo/$TIMEZONE" /etc/localtime
hwclock --systohc
gen_locales

log "host"
echo "$HOST" > /etc/hostname
cat > /etc/hosts <<HOSTS
127.0.0.1 localhost
::1 localhost
127.0.1.1 $HOST.localdomain $HOST
HOSTS

log "pacman"
tune_pacman /etc/pacman.conf

log "users"
echo "root:$PASSWORD" | chpasswd
id "$USERNAME" &>/dev/null || useradd -m -G wheel -s /usr/bin/zsh "$USERNAME"
echo "$USERNAME:$PASSWORD" | chpasswd

if [[ ${CONDA:-0} == 1 ]]; then
	log "miniforge"
	install_miniforge "$USERNAME"
fi

log "swap"
if [[ ! -f /swapfile ]]; then
	mkswap -U clear --size "$SWAP_SIZE" --file /swapfile
	echo '/swapfile none swap defaults 0 0' >> /etc/fstab
fi

log "initramfs"
mkinitcpio -P

log "cursor: vimix"
install_cursor

log "grub theme"
/root/install/grub-theme.sh /root/install/grub-theme

log "yay"
echo "$USERNAME ALL=(ALL) NOPASSWD: ALL" > /etc/sudoers.d/99-install
install_yay "$USERNAME"
rm -f /etc/sudoers.d/99-install

log "grub"
if [[ -n ${GRUB_CMDLINE_EXTRA:-} ]]; then
	sed -i -E "s|^GRUB_CMDLINE_LINUX_DEFAULT=\"(.*)\"|GRUB_CMDLINE_LINUX_DEFAULT=\"\1 $GRUB_CMDLINE_EXTRA\"|" /etc/default/grub
fi
grub-install --target=x86_64-efi --efi-directory=/efi --bootloader-id=GRUB
grub-install --target=x86_64-efi --efi-directory=/efi --removable
grub-mkconfig -o /boot/grub/grub.cfg

log "services"
enable_services
[[ ${VM:-0} == 1 ]] && systemctl enable sshd
true
