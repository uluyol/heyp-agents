package proc

import (
	"bufio"
	"io"
	"io/fs"

	pb "github.com/uluyol/heyp-agents/go/proto"
	"google.golang.org/protobuf/encoding/protojson"
)

type StatsRecReader struct {
	err error
	s   *bufio.Scanner
	rec *pb.StatsRecord
}

func NewStatsRecReader(r io.Reader) *StatsRecReader {
	return &StatsRecReader{
		s: bufio.NewScanner(r),
	}
}

func (r *StatsRecReader) Scan() bool {
	if r.err != nil {
		return false
	}
	if !r.s.Scan() {
		return false
	}
	r.rec = new(pb.StatsRecord)
	r.err = protojson.Unmarshal(r.s.Bytes(), r.rec)
	return r.err == nil
}

func (r *StatsRecReader) Record() *pb.StatsRecord { return r.rec }

func (r *StatsRecReader) Err() error {
	if r.err != nil {
		return r.err
	}
	return r.s.Err()
}

func ForEachStatsRec(err *error, fsys fs.FS, path string,
	fn func(*pb.StatsRecord) error) {

	if *err != nil {
		return
	}
	f, e := fsys.Open(path)
	if e != nil {
		*err = e
		return
	}
	defer f.Close()

	r := NewStatsRecReader(f)
	for r.Scan() {
		if e := fn(r.Record()); e != nil {
			*err = e
			return
		}
	}
	*err = r.Err()
}
