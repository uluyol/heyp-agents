package virt

import (
	_ "embed"
	"path"

	"github.com/uluyol/heyp-agents/go/virt/firecracker"
)

//go:embed image.tar.gz
var imageTar []byte

func ImageTarball() []byte { return imageTar }

func ImageData(topdir string) firecracker.ImageData {
	return firecracker.ImageData{
		RootDrivePath: path.Join(topdir, "image/rootfs"),
		KernelPath:    path.Join(topdir, "image/vmlinux"),
		SecretKeyPath: path.Join(topdir, "image/rootfs.id_rsa"),
	}
}
