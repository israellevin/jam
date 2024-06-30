#!/usr/bin/bash
IMAGE=alpine
TAG=latest
IMAGE_URL="https://auth.docker.io/token?service=registry.docker.io&scope=repository:library/$IMAGE:pull"
MANIFEST_URL="https://registry-1.docker.io/v2/library/$IMAGE/manifests/$TAG"
FIRECRACKER_RELEASE_BASE_URL="https://github.com/firecracker-microvm/firecracker/releases/download"
FIRECRACKER_RELEASE_URL="$FIRECRACKER_RELEASE_BASE_URL/v1.7.0/firecracker-v1.7.0-x86_64.tgz"

extract_docker_image_to_directory() {
    token=$(curl -s "$IMAGE_URL" | jq -r .token)
    mkdir rootfs
    curl -s "$MANIFEST_URL" \
        -H "Authorization: Bearer $token" \
        -H 'Accept: application/vnd.docker.distribution.manifest.v2+json' | jq -r '.layers[].digest' | \
            while read digest; do
                curl -sLH "Authorization: Bearer $token" \
                    "https://registry-1.docker.io/v2/library/$IMAGE/blobs/$digest" | tar -xzC rootfs/
            done
}

write_directory_to_image() {
    rootfs_size=$(du -sB1 rootfs | cut -f1)
    initial_image_size=$((rootfs_size))
    dd if=/dev/zero of=generator_base.ext4 bs=1 count=0 seek=$initial_image_size
    mkfs.ext4 -d rootfs generator_base.ext4
    resize2fs -M generator_base.ext4
    rm -rf rootfs
}

get_firecracker() {
    if [ ! -x firecracker ]; then
        echo "Downloading Firecracker"
        curl -L "$FIRECRACKER_RELEASE_URL" | tar xz --wildcards 'release-*/firecracker-*x86_64'
        find ./release-* -type f -executable -exec mv {} firecracker \;
        rm -rf release-*
    fi
    if [ ! -f vmlinux ]; then
        echo "Downloading vmlinux"
        curl -L https://s3.amazonaws.com/spec.ccfc.min/firecracker-ci/v1.9/x86_64/vmlinux-5.10.217 > vmlinux
    fi
}

install_generator_base_in_directory() {
    chroot rootfs /bin/sh <<'EOF'
echo nameserver 1.1.1.1 > /etc/resolv.conf
chmod 644 /etc/resolv.conf
apk add openrc
apk add util-linux
ln -s agetty /etc/init.d/agetty.ttyS0
echo ttyS0 > /etc/securetty
rc-update add agetty.ttyS0 default
rc-update add devfs boot
rc-update add local boot
rc-update add procfs boot
rc-update add sysfs boot
mkdir /generator/
touch /etc/local.d/marmalade.start
chmod 755 /etc/local.d/marmalade.start
passwd -d root
EOF
    cat > "rootfs/etc/local.d/marmalade.start" <<'EOF'
#!/bin/sh -e
mount -t tmpfs none /generator
exec > /generator/setup.log 2>&1
ip addr add 172.16.0.2/24 dev eth0
ip link set eth0 up
ip route add default via 172.16.0.1 dev eth0
echo ok | nc -lp2468 | tar -xzC /generator
for file in /generator/*; do
    if [ -x "$file" ]; then
        exec "$file"
        exit $?
    fi
done
EOF
}

[[ "${BASH_SOURCE[0]}" != "${0}" ]] && return 0
set -e
get_firecracker
extract_docker_image_to_directory
install_generator_base_in_directory
write_directory_to_image
exit 0
