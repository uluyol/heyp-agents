package proc

import (
	"encoding/json"
	"fmt"
	"io"
	"io/fs"
	"log"
	"os"
	"time"

	"github.com/uluyol/heyp-agents/go/proc/logs"
	"github.com/uluyol/heyp-agents/go/stats"
	"google.golang.org/protobuf/encoding/protojson"
	"google.golang.org/protobuf/proto"
)

type NamedLog struct {
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

func AlignProto(args AlignArgs, mkReader func(string, io.Reader) TSBatchReader) error {
	if args.Debug {
		log.Printf("AlignProto: inputs = %v", args.Inputs)
	}

	files := make([]fs.File, len(args.Inputs))
	readers := make([]TSBatchReader, len(args.Inputs))
	defer func() {
		for _, f := range files {
			if f != nil {
				f.Close()
			}
		}
	}()
	for i, arg := range args.Inputs {
		var err error
		files[i], err = args.FS.Open(arg.Path)
		if err != nil {
			return fmt.Errorf("failed to open input %s: %w", arg.Name, err)
		}
		readers[i] = mkReader(arg.Path, files[i])
	}

	fout, err := os.Create(args.Output)
	if err != nil {
		return fmt.Errorf("failed to create output: %w", err)
	}
	defer fout.Close()

	merger := NewTSMerger(args.Prec, readers, args.Debug)
	var tstamp time.Time
	data := make([]interface{}, len(files))

	var rec alignedRec
	for merger.Next(&tstamp, data) {
		if tstamp.Before(args.Start) {
			if args.Debug {
				log.Printf("discarding record at time %s: too early", tstamp)
			}
			continue
		}
		if args.End.Before(tstamp) {
			if args.Debug {
				log.Printf("discarding record at time %s: too late", tstamp)
			}
			break
		}

		rec.Reset()
		rec.UnixSec = unixSec(tstamp)

		_ = data[len(args.Inputs)-1]
		for i := range data {
			rec.Data[args.Inputs[i].Name] = protoMesg{data[i].(proto.Message)}
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

type AlignArgs struct {
	FS         fs.FS
	Inputs     []NamedLog
	Output     string
	Start, End time.Time
	Prec       time.Duration
	Debug      bool
}

// processRec cannot own the input rec.
func AlignHostStats(args AlignArgs, mkReader func(io.Reader) TSBatchReader, processRec func(*AlignedHostStatsRec)) error {
	files := make([]fs.File, len(args.Inputs))
	readers := make([]TSBatchReader, len(args.Inputs))
	defer func() {
		for _, f := range files {
			if f != nil {
				f.Close()
			}
		}
	}()
	for i, arg := range args.Inputs {
		var err error
		files[i], err = args.FS.Open(arg.Path)
		if err != nil {
			return fmt.Errorf("failed to open input %s: %w", arg.Name, err)
		}
		readers[i] = mkReader(files[i])
	}

	fout, err := os.Create(args.Output)
	if err != nil {
		return fmt.Errorf("failed to create output: %w", err)
	}
	defer fout.Close()

	merger := NewTSMerger(args.Prec, readers, args.Debug)
	var tstamp time.Time
	data := make([]interface{}, len(files))

	var rec AlignedHostStatsRec
	for merger.Next(&tstamp, data) {
		if tstamp.Before(args.Start) {
			continue
		}
		if args.End.Before(tstamp) {
			break
		}

		rec.Reset()
		rec.UnixSec = unixSec(tstamp)

		_ = data[len(args.Inputs)-1]
		for i := range data {
			rec.Data[args.Inputs[i].Name] = data[i].(*stats.HostStats)
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
func AlignHostEnforcerLogs(args AlignArgs, hostDC, nodeIP map[string]string) error {
	readers := make([]TSBatchReader, len(args.Inputs))
	for i, arg := range args.Inputs {
		var err error
		logDirFS, err := fs.Sub(args.FS, arg.Path)
		if err != nil {
			return fmt.Errorf("failed to open input %q: %w", arg.Path, err)
		}
		srcIP := nodeIP[arg.Name]
		readers[i], err = logs.NewHostEnforcerLogReader(logDirFS, hostDC, srcIP)
		if err != nil {
			return fmt.Errorf("failed to open reader on input %q: %w", arg.Path, err)
		}
	}

	fout, err := os.Create(args.Output)
	if err != nil {
		return fmt.Errorf("failed to create output: %w", err)
	}
	defer fout.Close()

	merger := NewTSMerger(args.Prec, readers, args.Debug)
	var tstamp time.Time
	data := make([]interface{}, len(readers))

	var rec AlignedEnforcerLogs
	for merger.Next(&tstamp, data) {
		if tstamp.Before(args.Start) {
			continue
		}
		if args.End.Before(tstamp) {
			break
		}

		rec.Reset()
		rec.UnixSec = unixSec(tstamp)

		_ = data[len(args.Inputs)-1]
		for i := range data {
			rec.Data[args.Inputs[i].Name] = data[i].(*logs.HostEnforcerLogEntry)
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
