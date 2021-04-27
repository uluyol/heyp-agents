package proc

import (
	"bytes"
	"fmt"
	"io"
	"os"
	"os/exec"
)

type sortedLineReader struct {
	remaining [][]byte
	lines     [][]byte

	lastSecondMin []byte
	lastMinIdx    int
}

func (r *sortedLineReader) printDebug() {
	fmt.Fprintf(os.Stderr, "r.remaining = %v\nr.lines = %v\n", r.remaining, r.lines)
}

func (r *sortedLineReader) drained() bool { return len(r.lines) == 0 }

func (r *sortedLineReader) init(data [][]byte) {
	r.remaining = data
	r.lines = make([][]byte, len(r.remaining))

	for i := 0; i < len(r.remaining); {
		if len(r.remaining) == 0 {
			r.remaining = append(r.remaining[:i], r.remaining[i+1:]...)
			r.lines = append(r.lines[:i], r.lines[i+1:]...)
		}
		line := r.remaining[i]
		nlIndex := bytes.IndexByte(line, '\n')
		if nlIndex != -1 {
			line = line[:nlIndex+1]
		}
		r.remaining[i] = r.remaining[i][len(line):]
		r.lines[i] = line
		i++
	}

	r.lastMinIdx = -1
}

func (r *sortedLineReader) consumeMinLine() []byte {
	if r.lastMinIdx >= 0 {
		if bytes.Compare(r.lastSecondMin, r.lines[r.lastMinIdx]) > 0 {
			b := r.lines[r.lastMinIdx]
			r.getNextLine(r.lastMinIdx)
			return b
		}
	}

	minIdx := 0
	for i := 1; i < len(r.lines); i++ {
		if bytes.Compare(r.lines[minIdx], r.lines[i]) > 0 {
			r.lastSecondMin = r.lines[minIdx]
			minIdx = i
		}
	}

	r.lastMinIdx = minIdx
	b := r.lines[minIdx]
	r.getNextLine(r.lastMinIdx)
	return b
}

func (r *sortedLineReader) getNextLine(i int) {
	if len(r.remaining) != len(r.lines) {
		panic(fmt.Errorf("len(r.remaining) = %d, len(r.lines) = %d", len(r.remaining), len(r.lines)))
	}

	if len(r.remaining[i]) == 0 {
		r.remaining = append(r.remaining[:i], r.remaining[i+1:]...)
		r.lines = append(r.lines[:i], r.lines[i+1:]...)
		r.lastMinIdx = -1
		return
	}

	line := r.remaining[i]
	nlIndex := bytes.IndexByte(line, '\n')
	if nlIndex != -1 {
		line = line[:nlIndex+1]
	}
	r.remaining[i] = r.remaining[i][len(line):]
	r.lines[i] = line
}

func SortedPrint(w io.Writer, bufs [][]byte) error {
	var r sortedLineReader
	r.init(bufs)
	for !r.drained() {
		if _, err := w.Write(r.consumeMinLine()); err != nil {
			return err
		}
	}
	return nil
}

func SortedPrintTable(w io.Writer, bufs [][]byte, sep string) error {
	var sortInput bytes.Buffer
	for _, b := range bufs {
		sortInput.Write(b)
	}
	var sortOutput bytes.Buffer
	var sortErr bytes.Buffer

	cmd := exec.Command("sort", "-g", "-t", sep)
	cmd.Stdin = &sortInput
	cmd.Stdout = &sortOutput
	cmd.Stderr = &sortErr
	if err := cmd.Run(); err != nil {
		return fmt.Errorf("failed to sort data: %w\nfull ouput: %s", err, sortErr.Bytes())
	}

	_, err := io.Copy(os.Stdout, &sortOutput)
	return err
}
