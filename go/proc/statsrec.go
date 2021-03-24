package proc

import (
	"bufio"
	"bytes"
	"fmt"
	"io"
	"io/fs"

	pb "github.com/uluyol/heyp-agents/go/proto"
	"google.golang.org/protobuf/encoding/protojson"
)

type StatsRecReader struct {
	err error
	r   *bufio.Reader
	rec *pb.StatsRecord
}

func NewStatsRecReader(r io.Reader) *StatsRecReader {
	return &StatsRecReader{
		r: bufio.NewReader(r),
	}
}

func (r *StatsRecReader) Scan() bool {
	if r.err != nil {
		return false
	}
	var (
		line     []byte
		isPrefix = true
		gotEOF   bool
	)
	for isPrefix && r.err == nil {
		var cur []byte
		cur, isPrefix, r.err = r.r.ReadLine()
		line = append(line, cur...)
		if r.err == io.EOF {
			r.err = nil
			gotEOF = true
			break
		}
	}
	if r.err != nil {
		return false
	}
	if len(bytes.TrimSpace(line)) == 0 {
		if gotEOF {
			return false
		}
		return r.Scan() // skip this empty line
	}
	r.rec = new(pb.StatsRecord)
	r.err = protojson.Unmarshal(line, r.rec)
	return r.err == nil
}

func (r *StatsRecReader) Record() *pb.StatsRecord { return r.rec }

func (r *StatsRecReader) Err() error {
	return r.err
}

func ForEachStatsRec(err *error, fsys fs.FS, path string,
	fn func(*pb.StatsRecord) error) {
	if *err != nil {
		return
	}
	f, e := fsys.Open(path)
	if e != nil {
		*err = fmt.Errorf("failed to open %s: %w", path, e)
		return
	}
	defer f.Close()

	r := NewStatsRecReader(f)
	for r.Scan() {
		if e := fn(r.Record()); e != nil {
			*err = fmt.Errorf("error processing %s: %w", path, e)
			return
		}
	}
	if r.Err() != nil {
		*err = fmt.Errorf("failed to read %s: %w", path, r.Err())
	}
}
