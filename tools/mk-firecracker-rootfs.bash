#!/usr/bin/env bash
#
# WIP; ADDED FOR POSTERITY
#
# Derived from https://github.com/oraoto/archlinux-firecracker/blob/master/scripts/build-arch-rootfs.sh

set -e

# Docker env variables
DISK_SIZE=4G
DISK_FILE=/arch-rootfs.ext4
DISK_ROOT=/mnt

TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT

cat >"$TMPDIR/mkroot.bash" <<EOF
#!/bin/bash

set -e

echo == INSTALL BUILD DEPS ==
pacman -Sy --noconfirm arch-install-scripts git sudo

echo == ALLOCATE ROOT FS ==
fallocate -l 4G $DISK_FILE
mkfs.ext4 $DISK_FILE

# Mount rootfs to mount
mkdir -p $DISK_ROOT
sudo mount $DISK_FILE $DISK_ROOT

echo == INSTALL PACKAGES ==
yes y | sudo pacstrap -i -c $DISK_ROOT bash filesystem systemd-sysvcompat pacman iproute2 openssh

echo == CONFIGURE NETWORK AND SSH ==
echo "nameserver 1.1.1.1" | sudo tee $DISK_ROOT/etc/resolv.conf

sudo mkdir -p $DISK_ROOT/root/.ssh
sudo tee $DISK_ROOT/root/.ssh/authorized_keys >/dev/null <<_EOF
ssh-rsa AAAAB3NzaC1yc2EAAAADAQABAAABAQDPwnddWF7+WOV8XqUtopQglNwKq0+3uVN0WBs3QEhwHUjjdqwz86soAYF2WgHpIJfZAeu+lKS0yho6q8HzWbpZxMpWCJNR8oQLNfK17mmRQnO+C249cU+gzsoCsgNkCrYWXAnyLqzux7IdTBzgkPZqdKL0ij9hcvOBxS8ELtq3pPSID0jCSkqlkKeXn6p/zByo300yVxmXzxth7mugHwdbd7UAA+jdyUJZU8nnFg3nJft7WSQke2DhFnbyg58RTUWQakOPXlD+19kp8bUyW4yLvmI7N81ni2Pjq2g97oi2bdFDVrkMXOiejmVzvYE/40OhH7Yc89i4hyJY0IYmcf7B
_EOF
sudo chmod 644 $DISK_ROOT/root/.ssh/authorized_keys

echo "PermitRootLogin yes" | sudo tee -a $DISK_ROOT/etc/ssh/sshd_config >/dev/null

# sudo tee $DISK_ROOT/etc/systemd/system/firecracker-network.service <<-'EOF'
# [Unit]
# Description=Firecracker Network
# [Service]
# Type=oneshot
# ExecStart=ip link set eth0 up
# ExecStart=ip addr add 172.16.0.2/24 dev eth0
# ExecStart=ip route add default via 172.16.0.1 dev eth0
# RemainAfterExit=yes
# [Install]
# WantedBy=multi-user.target
# EOF

# sudo ln -s /etc/systemd/system/firecracker-network.service $DISK_ROOT/etc/systemd/system/multi-user.target.wants/
sudo ln -s /etc/systemd/system/sshd.service $DISK_ROOT/etc/systemd/system/multi-user.target.wants/

# Remove default (locked) root password
# See https://github.com/archlinux/svntogit-packages/commit/0320c909f3867d47576083e853543bab1705185b

sudo sed 's/^root:.*/root::14871::::::/' -i $DISK_ROOT/etc/shadow

sudo umount $DISK_ROOT
rmdir $DISK_ROOT

echo == COPY OUT ROOTFS ==
cp /arch-rootfs.ext4 /out/image/rootfs
EOF

pushd "$TMPDIR" >/dev/null
mkdir image

echo == ENTER DOCKER ENV ==
docker run -v $PWD:/out --rm --privileged archlinux bash /out/mkroot.bash

echo == FETCH KERNEL ==
wget -q -O image/rootfs.id_rsa https://raw.githubusercontent.com/firecracker-microvm/firecracker-demo/c1981fce9ab5019b1c213161f285659b0d525ec7/xenial.rootfs.id_rsa
wget -q -O image/vmlinux https://raw.githubusercontent.com/firecracker-microvm/firecracker-demo/c1981fce9ab5019b1c213161f285659b0d525ec7/vmlinux

chmod 600 image/rootfs.id_rsa

echo == CREATE ARCHIVE ==
tar czf image.tar.gz image/vmlinux image/rootfs image/rootfs.id_rsa
popd >/dev/null

echo == MOVE ARCHIVE TO FINAL DEST ==
mv "$TMPDIR/image.tar.gz" go/deploy/virt/image.tar.gz
