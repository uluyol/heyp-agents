package proc

import (
	"encoding/json"
	"fmt"
	"io"
	"io/fs"
	"os"
	"time"

	"github.com/uluyol/heyp-agents/go/proc/logs"
	"github.com/uluyol/heyp-agents/go/stats"
	"google.golang.org/protobuf/encoding/protojson"
	"google.golang.org/protobuf/proto"
)

type ToAlign struct {
	Name string
	Path string
}

type protoMesg struct {
	m proto.Message
}

func (m protoMesg) MarshalJSON() ([]byte, error) {
	return protojson.MarshalOptions{
		EmitUnpopulated: true,
	}.Marshal(m.m)
}

var _ json.Marshaler = protoMesg{}

type alignedRec struct {
	UnixSec float64              `json:"unixSec"`
	Data    map[string]protoMesg `json:"data"`
}

func (r *alignedRec) Reset() {
	if r.Data == nil {
		r.Data = make(map[string]protoMesg)
	}
	for k := range r.Data {
		delete(r.Data, k)
	}
}

func AlignProto(fsys fs.FS, inputs []ToAlign, mkReader func(io.Reader) TSBatchReader, output string,
	start, end time.Time, prec time.Duration) error {

	files := make([]fs.File, len(inputs))
	readers := make([]TSBatchReader, len(inputs))
	defer func() {
		for _, f := range files {
			if f != nil {
				f.Close()
			}
		}
	}()
	for i, arg := range inputs {
		var err error
		files[i], err = fsys.Open(arg.Path)
		if err != nil {
			return fmt.Errorf("failed to open input %s: %w", arg.Name, err)
		}
		readers[i] = mkReader(files[i])
	}

	fout, err := os.Create(output)
	if err != nil {
		return fmt.Errorf("failed to create output: %w", err)
	}
	defer fout.Close()

	merger := NewTSMerger(prec, readers)
	var tstamp time.Time
	data := make([]interface{}, len(files))

	var rec alignedRec
	for merger.Next(&tstamp, data) {
		if tstamp.Before(start) {
			continue
		}
		if end.Before(tstamp) {
			break
		}

		rec.Reset()
		rec.UnixSec = unixSec(tstamp)

		_ = data[len(inputs)-1]
		for i := range data {
			rec.Data[inputs[i].Name] = protoMesg{data[i].(proto.Message)}
		}
		buf, err := json.Marshal(&rec)
		if err != nil {
			return fmt.Errorf("failed to marshal data: %w", err)
		}
		_, err = fout.Write(buf)
		if err != nil {
			return fmt.Errorf("failed to write data: %w", err)
		}
		_, err = fout.WriteString("\n")
		if err != nil {
			return fmt.Errorf("failed to write data: %w", err)
		}
	}

	if err := merger.Err(); err != nil {
		return fmt.Errorf("failed to merge: %w", err)
	}
	return nil
}

type AlignedHostStatsRec struct {
	UnixSec float64                     `json:"unixSec"`
	Data    map[string]*stats.HostStats `json:"data"`
}

func (r *AlignedHostStatsRec) Reset() {
	if r.Data == nil {
		r.Data = make(map[string]*stats.HostStats)
	}
	for k := range r.Data {
		delete(r.Data, k)
	}
}

// processRec cannot own the input rec.
func AlignHostStats(fsys fs.FS, inputs []ToAlign, mkReader func(io.Reader) TSBatchReader, output string, processRec func(*AlignedHostStatsRec),
	start, end time.Time, prec time.Duration) error {

	files := make([]fs.File, len(inputs))
	readers := make([]TSBatchReader, len(inputs))
	defer func() {
		for _, f := range files {
			if f != nil {
				f.Close()
			}
		}
	}()
	for i, arg := range inputs {
		var err error
		files[i], err = fsys.Open(arg.Path)
		if err != nil {
			return fmt.Errorf("failed to open input %s: %w", arg.Name, err)
		}
		readers[i] = mkReader(files[i])
	}

	fout, err := os.Create(output)
	if err != nil {
		return fmt.Errorf("failed to create output: %w", err)
	}
	defer fout.Close()

	merger := NewTSMerger(prec, readers)
	var tstamp time.Time
	data := make([]interface{}, len(files))

	var rec AlignedHostStatsRec
	for merger.Next(&tstamp, data) {
		if tstamp.Before(start) {
			continue
		}
		if end.Before(tstamp) {
			break
		}

		rec.Reset()
		rec.UnixSec = unixSec(tstamp)

		_ = data[len(inputs)-1]
		for i := range data {
			rec.Data[inputs[i].Name] = data[i].(*stats.HostStats)
		}
		buf, err := json.Marshal(&rec)
		if err != nil {
			return fmt.Errorf("failed to marshal data: %w", err)
		}
		_, err = fout.Write(buf)
		if err != nil {
			return fmt.Errorf("failed to write data: %w", err)
		}
		_, err = fout.WriteString("\n")
		if err != nil {
			return fmt.Errorf("failed to write data: %w", err)
		}
		if processRec != nil {
			processRec(&rec)
		}
	}

	if err := merger.Err(); err != nil {
		return fmt.Errorf("failed to merge: %w", err)
	}
	return nil
}

type AlignedEnforcerLogs struct {
	UnixSec float64                               `json:"unixSec"`
	Data    map[string]*logs.HostEnforcerLogEntry `json:"data"`
}

func (r *AlignedEnforcerLogs) Reset() {
	if r.Data == nil {
		r.Data = make(map[string]*logs.HostEnforcerLogEntry)
	}
	for k := range r.Data {
		delete(r.Data, k)
	}
}

// processRec cannot own the input rec.
func AlignHostEnforcerLogs(fsys fs.FS, inputs []ToAlign, output string,
	hostDC, nodeIP map[string]string, start, end time.Time, prec time.Duration) error {

	readers := make([]TSBatchReader, len(inputs))
	for i, arg := range inputs {
		var err error
		logDirFS, err := fs.Sub(fsys, arg.Path)
		if err != nil {
			return fmt.Errorf("failed to open input %q: %w", arg.Path, err)
		}
		srcIP := nodeIP[arg.Name]
		readers[i], err = logs.NewHostEnforcerLogReader(logDirFS, hostDC, srcIP)
		if err != nil {
			return fmt.Errorf("failed to open reader on input %q: %w", arg.Path, err)
		}
	}

	fout, err := os.Create(output)
	if err != nil {
		return fmt.Errorf("failed to create output: %w", err)
	}
	defer fout.Close()

	merger := NewTSMerger(prec, readers)
	var tstamp time.Time
	data := make([]interface{}, len(readers))

	var rec AlignedEnforcerLogs
	for merger.Next(&tstamp, data) {
		if tstamp.Before(start) {
			continue
		}
		if end.Before(tstamp) {
			break
		}

		rec.Reset()
		rec.UnixSec = unixSec(tstamp)

		_ = data[len(inputs)-1]
		for i := range data {
			rec.Data[inputs[i].Name] = data[i].(*logs.HostEnforcerLogEntry)
		}
		buf, err := json.Marshal(&rec)
		if err != nil {
			return fmt.Errorf("failed to marshal data: %w", err)
		}
		_, err = fout.Write(buf)
		if err != nil {
			return fmt.Errorf("failed to write data: %w", err)
		}
		_, err = fout.WriteString("\n")
		if err != nil {
			return fmt.Errorf("failed to write data: %w", err)
		}
	}

	if err := merger.Err(); err != nil {
		return fmt.Errorf("failed to merge: %w", err)
	}
	return nil
}

func unixSec(t time.Time) float64 {
	sec := float64(t.Unix())
	ns := float64(t.Nanosecond())
	return sec + (ns / 1e9)
}
