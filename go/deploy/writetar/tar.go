package writetar

import (
	"archive/tar"
	"bytes"
	"fmt"
	"io"
	"os"
	"os/exec"
)

type FileToTar struct {
	name string
	data []byte
}

func Add(name string, data []byte) FileToTar {
	return FileToTar{name: name, data: data}
}

func ConcatInMem(files ...FileToTar) []byte {
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

type XZWriter struct {
	fout   *os.File
	xzCmd  *exec.Cmd
	xzPipe io.WriteCloser
	w      *tar.Writer
	err    error
}

type Input struct {
	Dest      string
	InputPath string
}

func NewXZWriter(destPath string) (*XZWriter, error) {
	w := new(XZWriter)
	w.xzCmd = exec.Command("xz", "-z", "-k")
	var err error
	w.xzPipe, err = w.xzCmd.StdinPipe()
	if err != nil {
		return nil, err
	}
	w.w = tar.NewWriter(w.xzPipe)

	w.fout, err = os.Create(destPath)
	if err != nil {
		return nil, err
	}

	w.xzCmd.Stdout = w.fout

	err = w.xzCmd.Start()
	if err != nil {
		w.fout.Close()
		return nil, fmt.Errorf("failed to start xz: %w", err)
	}

	return w, nil
}

func (w *XZWriter) Add(input Input) {
	if w.err != nil {
		return
	}

	f, err := os.Open(input.InputPath)
	if err != nil {
		w.err = fmt.Errorf("failed to open input file %s: %w", input.InputPath, err)
		return
	}
	defer f.Close()

	fi, err := f.Stat()
	if err != nil {
		w.err = fmt.Errorf("failed to stat input file %s: %w", input.InputPath, err)
		return
	}

	w.w.WriteHeader(&tar.Header{
		Name: input.Dest,
		Mode: 0644,
		Size: fi.Size(),
	})

	_, err = io.Copy(w.w, f)
	if err != nil {
		w.err = fmt.Errorf("error while compressing %s: %w", input.Dest, err)
		return
	}
}

func (w *XZWriter) AddAll(inputs []Input) {
	for _, input := range inputs {
		w.Add(input)
	}
}

func (w *XZWriter) Close() error {
	e := w.w.Close()
	if e != nil && w.err == nil {
		w.err = fmt.Errorf("failed to close tar writer: %w", e)
	}
	e = w.xzPipe.Close()
	if e != nil && w.err == nil {
		w.err = fmt.Errorf("failed to close xz input pipe: %w", e)
	}
	e = w.xzCmd.Wait()
	if e != nil && w.err == nil {
		w.err = fmt.Errorf("xz command failed: %w", e)
	}
	e = w.fout.Close()
	if e != nil && w.err == nil {
		w.err = fmt.Errorf("failed to close output file: %w", e)
	}
	return w.err
}
