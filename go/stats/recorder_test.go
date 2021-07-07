package stats_test

import (
	"bytes"
	"math"
	"sort"
	"testing"
	"testing/fstest"
	"time"

	"github.com/uluyol/heyp-agents/go/pb"
	"github.com/uluyol/heyp-agents/go/proc"
	"github.com/uluyol/heyp-agents/go/stats"
	"google.golang.org/protobuf/encoding/prototext"
)

type ClosableBuffer struct {
	*bytes.Buffer
}

func (ClosableBuffer) Close() error { return nil }

func flatten(h *pb.HdrHistogram) []int64 {
	var data []int64
	for _, b := range h.GetBuckets() {
		v := b.GetV()
		for i := 0; i < int(b.GetC()); i++ {
			data = append(data, v)
		}
	}
	return data
}

type int64s []int64

func (x int64s) Len() int           { return len(x) }
func (x int64s) Less(i, j int) bool { return x[i] < x[j] }
func (x int64s) Swap(i, j int)      { x[i], x[j] = x[j], x[i] }

func hasHist(hists []*pb.StatsRecord_LatencyStats, label string, data []int64) bool {
	for _, h := range hists {
		if h.GetKind() == label {
			gotData := flatten(h.GetHistNs())
			sort.Sort(int64s(gotData))
			sort.Sort(int64s(data))

			if len(data) != len(gotData) {
				return false
			}

			for i := range data {
				x := float64(data[i])
				y := float64(gotData[i])

				err := x / 100

				if math.Abs(x-y) > err {
					return false
				}
			}

			return true
		}
	}
	return false
}

type wantLatency struct {
	Kind string
	Data []int64
}

type wantRecord struct {
	Label             string
	CumNumBits        int64
	CumNumRPCs        int64
	NonZeroDurSec     bool
	NonEmptyTimestamp bool
	Latencies         []wantLatency
}

func checkExpected(t *testing.T, got *pb.StatsRecord, want wantRecord) {
	t.Helper()

	if got.GetLabel() != want.Label {
		t.Errorf("wanted record to be for %q: got label %q", want.Label, got.GetLabel())
		return
	}

	t.Logf("%s: got\n%s", want.Label, prototext.Format(got))

	if x := got.GetCumNumBits(); x != want.CumNumBits {
		t.Errorf("%s CumNumBits: got %d want %d", want.Label, x, want.CumNumBits)
	}

	if x := got.GetCumNumRpcs(); x != want.CumNumRPCs {
		t.Errorf("%s CumNumRPCs: got %d want %d", want.Label, x, want.CumNumRPCs)
	}

	if want.NonZeroDurSec && got.GetDurSec() == 0 {
		t.Errorf("%s DurSec is zero", want.Label)
	}

	if want.NonEmptyTimestamp && got.GetTimestamp() == "" {
		t.Errorf("%s Timestamp is empty", want.Label)
	}

	if len(got.GetLatency()) != len(want.Latencies) {
		t.Errorf("%s Latency: wrong number of entries", want.Label)
		return
	}

	for _, lat := range want.Latencies {
		if !hasHist(got.GetLatency(), lat.Kind, lat.Data) {
			t.Errorf("%s Latency hist %s is incorrect", want.Label, lat.Kind)
		}
	}
}

func TestRecorderReadBack(t *testing.T) {
	var buf bytes.Buffer
	rec := stats.NewRecorder(ClosableBuffer{&buf})
	rec.StartRecording()
	rec.RecordRPC2(100, "e2e", 5*time.Millisecond, "net", 2*time.Millisecond)
	rec.RecordRPC2(200, "e2e", 40*time.Millisecond, "net", 2*time.Millisecond)
	rec.DoneStep("step1")
	rec.RecordRPC2(10000, "e2e", 1*time.Second, "net", 60*time.Millisecond)
	rec.DoneStep("step2")
	if err := rec.Close(); err != nil {
		t.Fatalf("unexpected error while closing: %v", err)
	}

	fsys := fstest.MapFS{
		"data.log.0": &fstest.MapFile{
			Data: buf.Bytes(),
			Mode: 0o644,
		},
	}

	var gotStep1 bool
	var gotStep2 bool

	var err error
	proc.ForEachStatsRec(&err, fsys, "data.log.0", func(r *pb.StatsRecord) error {
		switch {
		case !gotStep1:
			checkExpected(t, r, wantRecord{
				Label:             "step1",
				CumNumBits:        2400,
				CumNumRPCs:        2,
				NonZeroDurSec:     true,
				NonEmptyTimestamp: true,
				Latencies: []wantLatency{
					{Kind: "e2e", Data: []int64{5e6, 40e6}},
					{Kind: "net", Data: []int64{2e6, 2e6}},
				},
			})
			gotStep1 = true
		case !gotStep2:
			checkExpected(t, r, wantRecord{
				Label:             "step2",
				CumNumBits:        82400,
				CumNumRPCs:        3,
				NonZeroDurSec:     true,
				NonEmptyTimestamp: true,
				Latencies: []wantLatency{
					{Kind: "e2e", Data: []int64{1e9}},
					{Kind: "net", Data: []int64{60e6}},
				},
			})
			gotStep2 = true
		default:
			t.Errorf("found unexpected record: %v", r)
		}

		return nil
	})

	if err != nil {
		t.Errorf("unexpected error while reading: %v", err)
	}

	if !gotStep1 {
		t.Error("did not see step1 record")
	}
	if !gotStep2 {
		t.Error("did not see step2 record")
	}
}
