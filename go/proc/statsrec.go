package proc

import (
	"fmt"
	"io/fs"

	pb "github.com/uluyol/heyp-agents/go/proto"
)

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

	r := NewProtoJSONRecReader(f)
	for {
		rec := new(pb.StatsRecord)
		if !r.ScanInto(rec) {
			break
		}
		if e := fn(rec); e != nil {
			*err = fmt.Errorf("error processing %s: %w", path, e)
			return
		}
	}
	if r.Err() != nil {
		*err = fmt.Errorf("failed to read %s: %w", path, r.Err())
	}
}
