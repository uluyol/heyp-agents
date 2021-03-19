package actions

import (
	"archive/tar"
	"bytes"
)

type FileToTar struct {
	name string
	data []byte
}

func AddTar(name string, data []byte) FileToTar {
	return FileToTar{name: name, data: data}
}

func ConcatTarInMem(files ...FileToTar) []byte {
	var buf bytes.Buffer
	tw := tar.NewWriter(&buf)
	for _, f := range files {
		err := tw.WriteHeader(&tar.Header{
			Name: f.name,
			Mode: 0644,
			Size: int64(len(f.data)),
		})
		if err != nil {
			panic("failed to write header to tar file")
		}
		if _, err := tw.Write(f.data); err != nil {
			panic("failed to write data to tar file")
		}
	}
	if err := tw.Close(); err != nil {
		panic("failed to close tar writer")
	}
	return buf.Bytes()
}
