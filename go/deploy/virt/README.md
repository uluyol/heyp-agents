# Images

Kernel image and rootfs were taken from the firecracker getting stared guide and then compressed with gzip. [Link to kernel image].

```
wget https://cdn.kernel.org/pub/linux/kernel/v5.x/linux-5.10.88.tar.xz
tar xJf linux-5.10.88.tar.xz
cd linux-5.10.88/
# Copy microvm-kernel.config to .config
make -j8 vmlinux
```

```
mkdir image
wget -q -O image/rootfs https://raw.githubusercontent.com/firecracker-microvm/firecracker-demo/c1981fce9ab5019b1c213161f285659b0d525ec7/xenial.rootfs.ext4
wget -q -O image/rootfs.id_rsa https://raw.githubusercontent.com/firecracker-microvm/firecracker-demo/c1981fce9ab5019b1c213161f285659b0d525ec7/xenial.rootfs.id_rsa
# Copy vmlinux to image/vmlinux

chmod 600 image/rootfs.id_rsa
tar czf image.tar.gz image/vmlinux image/rootfs image/rootfs.id_rsa 
rm -rf image
```
