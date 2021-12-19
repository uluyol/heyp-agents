#!/bin/bash
#
# BROKEN
#
# An attempt to build a custom alpine image that worked on
# firecracker. Kept for posterity.

set -e

BASE_IMAGE=https://dl-cdn.alpinelinux.org/alpine/v3.15/releases/x86_64/alpine-minirootfs-3.15.0-x86_64.tar.gz
PKG_REPO=https://alpine.global.ssl.fastly.net/alpine/v3.15/main/x86_64

PKGS=(
    openrc-0.44.7-r2
    iproute2-minimal-5.15.0-r0
    iproute2-ss-5.15.0-r0
    iproute2-tc-5.15.0-r0
    iproute2-5.15.0-r0
    iproute2-doc-5.15.0-r0
    libmnl-1.0.4-r2
    libmnl-static-1.0.4-r2
    libnftnl-1.2.1-r0
    iptables-1.8.7-r1
    ip6tables-1.8.7-r1
    iptables-openrc-1.8.7-r1
    ip6tables-openrc-1.8.7-r1
)

CACHE=${CACHE:-cache}
WORK=${WORK:-work}
IMG=${IMG:-alpine.rootfs.squashfs}
SSH_KEY_PUB=${SSH_KEY_PUB:-alpine.rootfs.id_ed25519.pub}

install_deps() {
    sudo apt update || return 1
    sudo apt install -y squashfs-tools-ng || return 1
}

fetch() {
    mkdir -p "$CACHE"
    pushd "$CACHE" >/dev/null

    echo fetch base image
    wget -q "$BASE_IMAGE" || return 1

    for pkg in "${PKGS[@]}"; do
        echo fetch package $pkg
        wget -q "$PKG_REPO/$pkg.apk" || return 1
    done

    popd >/dev/null
}

build() {
    sudo rm -rf "$WORK"
    sudo mkdir -p "$WORK"
    echo extract base image
    sudo tar xzf "$CACHE/$(basename $BASE_IMAGE)" -C "$WORK" || return 1

    # echo use package mirror
    # sudo sed 's/dl-cdn.alpinelinux.org/alpine.global.ssl.fastly.net/' -i "$WORK/etc/apk/repositories"

    cat <<EOF | sudo tee "$WORK/etc/resolv.conf" >/dev/null
nameserver 1.1.1.1
EOF

    echo install packages
    sudo chroot "$WORK" apk update
    sudo chroot "$WORK" apk add bash openrc iproute2 iptables openssh util-linux
    sudo chroot "$WORK" ln -s agetty /etc/init.d/agetty.ttyS0
    sudo chroot "$WORK" /bin/sh -c "echo ttyS0 >/etc/securetty"
    sudo chroot "$WORK" /bin/sh -c "echo PermitRootLogin yes >>/etc/ssh/sshd_config"
    sudo chroot "$WORK" /bin/sh -c 'echo "auto eth0\niface eth0 inet manual" > /etc/network/interfaces'
    sudo chroot "$WORK" rc-update add agetty.ttyS0 default
    sudo chroot "$WORK" rc-update add sshd default
    sudo chroot "$WORK" rc-update add networking default
    sudo chroot "$WORK" rc-update add devfs boot
    sudo chroot "$WORK" rc-update add procfs boot
    sudo chroot "$WORK" rc-update add sysfs boot

    # for pkg in "${PKGS[@]}"; do
    #     echo extract package $pkg
    #     sudo tar xzf "$CACHE/$pkg.apk" -C "$WORK" || return 1
    # done

    echo set empty root password
    sudo sed 's/^root:.*/root::14871::::::/' -i "$WORK"/etc/shadow

    echo add authorized key
    sudo mkdir -p "$WORK/root/.ssh"
    sudo tee "$WORK/root/.ssh/authorized_keys" <"$SSH_KEY_PUB" >/dev/null
    sudo chmod 644 "$WORK/root/.ssh/authorized_keys"

    echo set default nameserver
    echo 'nameserver 1.1.1.1' | sudo tee "$WORK/etc/resolv.conf" >/dev/null

    (
        cd "$WORK"
        sudo tar cf - .
    ) | tar2sqfs -c lz4 "$IMG" || return 1
}

if [[ $# -ne 1 ]]; then
    echo "usage: $0 install-deps|fetch|build" >&2
    exit 3
fi

case $1 in
install-deps)
    install_deps
    ;;
fetch)
    fetch
    ;;
build)
    build
    ;;
esac
