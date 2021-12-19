# Images

Kernel image and rootfs were taken from the firecracker getting stared guide and then compressed with gzip. [Link to kernel image].

```
mkdir image
wget -q -O image/rootfs https://raw.githubusercontent.com/firecracker-microvm/firecracker-demo/c1981fce9ab5019b1c213161f285659b0d525ec7/xenial.rootfs.ext4
wget -q -O image/rootfs.id_rsa https://raw.githubusercontent.com/firecracker-microvm/firecracker-demo/c1981fce9ab5019b1c213161f285659b0d525ec7/xenial.rootfs.id_rsa
wget -q -O image/vmlinux https://raw.githubusercontent.com/firecracker-microvm/firecracker-demo/c1981fce9ab5019b1c213161f285659b0d525ec7/vmlinux

chmod 600 image/rootfs.id_rsa
tar czf image.tar.gz image/vmlinux image/rootfs image/rootfs.id_rsa 
rm -rf image
```
