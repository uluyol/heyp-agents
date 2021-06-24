package stats

import (
	"fmt"
	"io"
	"sort"
	"sync"
	"time"

	"github.com/HdrHistogram/hdrhistogram-go"
	"github.com/uluyol/heyp-agents/go/proto"
	"google.golang.org/protobuf/encoding/protojson"
	pbproto "google.golang.org/protobuf/proto"
)

type counters struct {
	cumNumBits int64
	cumNumRPCs int64
}

type Recorder struct {
	mu  sync.Mutex
	out io.WriteCloser
	err error

	cur   counters
	hists map[string]*hdrhistogram.Histogram

	prevTime time.Time
	prev     counters
	started  bool

	writeWG sync.WaitGroup
}

func NewRecorder(out io.WriteCloser) *Recorder {
	return &Recorder{out: out, hists: make(map[string]*hdrhistogram.Histogram)}
}

// StartRecording must be called before any other methods
// and cannot be called multiple times.
func (r *Recorder) StartRecording() {
	r.started = true
	r.prevTime = time.Now()
}

// must hold r.mu
func (r *Recorder) recordRPCSize(size int) {
	r.cur.cumNumBits += int64(size) * 8
	r.cur.cumNumRPCs++
}

func (r *Recorder) recordLatency(kind string, latency time.Duration) {
	hist, ok := r.hists[kind]
	if !ok {
		hist = hdrhistogram.New(100, 10_000_000_000, 2)
		r.hists[kind] = hist
	}
	hist.RecordValue(latency.Nanoseconds())
}

func (r *Recorder) RecordRPC1(rpcSizeBytes int, kind string, latency time.Duration) {
	if !r.started {
		panic("must call StartRecording before any other methods")
	}

	r.mu.Lock()
	defer r.mu.Unlock()

	r.recordRPCSize(rpcSizeBytes)
	r.recordLatency(kind, latency)
}

func (r *Recorder) RecordRPC2(rpcSizeBytes int, kind1 string, latency1 time.Duration, kind2 string, latency2 time.Duration) {
	if !r.started {
		panic("must call StartRecording before any other methods")
	}

	r.mu.Lock()
	defer r.mu.Unlock()

	r.recordRPCSize(rpcSizeBytes)
	r.recordLatency(kind1, latency1)
	r.recordLatency(kind2, latency2)
}

func (r *Recorder) DoneStep(label string) {
	if !r.started {
		panic("must call StartRecording before any other methods")
	}

	r.mu.Lock()
	defer r.mu.Unlock()

	now := time.Now()
	elapsedSec := now.Sub(r.prevTime).Seconds()
	meanBPS := float64(r.cur.cumNumBits-r.prev.cumNumBits) / elapsedSec
	meanRPS := float64(r.cur.cumNumRPCs-r.prev.cumNumRPCs) / elapsedSec
	counters := r.cur

	latencyData := toProtoLatencyStats(r.hists)

	r.writeWG.Wait()
	r.writeWG.Add(1)
	go func() {
		rec := &proto.StatsRecord{
			Label:     label,
			Timestamp: now.In(time.UTC).Format(time.RFC3339Nano),
			DurSec:    elapsedSec,

			CumNumBits: counters.cumNumBits,
			CumNumRpcs: counters.cumNumRPCs,

			MeanBitsPerSec: meanBPS,
			MeanRpcsPerSec: meanRPS,
			Latency:        latencyData,
		}

		err := writeJSONLine(r.out, rec)
		if err != nil {
			r.mu.Lock()
			if r.err == nil {
				r.err = err
			}
			r.mu.Unlock()
		}

		r.writeWG.Done()
	}()

	r.prevTime = now
	r.prev = r.cur
	for _, hist := range r.hists {
		hist.Reset()
	}
}

func (r *Recorder) Close() error {
	r.mu.Lock()
	r.writeWG.Wait()
	err := r.out.Close()
	if r.err != nil {
		err = r.err
	}
	r.mu.Unlock()

	return err
}

func toProtoLatencyStats(hists map[string]*hdrhistogram.Histogram) []*proto.StatsRecord_LatencyStats {
	stats := make([]*proto.StatsRecord_LatencyStats, 0, len(hists))
	for kind, hist := range hists {
		stats = append(stats, &proto.StatsRecord_LatencyStats{
			Kind:   kind,
			HistNs: HistToProto(hist),
			P50Ns:  hist.ValueAtPercentile(50),
			P90Ns:  hist.ValueAtPercentile(90),
			P95Ns:  hist.ValueAtPercentile(95),
			P99Ns:  hist.ValueAtPercentile(99),
		})
	}
	sort.Slice(stats, func(i, j int) bool {
		return stats[i].Kind < stats[j].Kind
	})
	return stats
}

func HistToProto(h *hdrhistogram.Histogram) *proto.HdrHistogram {
	ret := &proto.HdrHistogram{
		Config: &proto.HdrHistogram_Config{
			LowestDiscernibleValue: h.LowestTrackableValue(),
			HighestTrackableValue:  h.HighestTrackableValue(),
			SignificantFigures:     int32(h.SignificantFigures()),
		},
	}
	dist := h.Distribution()
	ret.Buckets = make([]*proto.HdrHistogram_Bucket, 0, h.TotalCount()/10)
	for _, bar := range dist {
		if bar.Count > 0 {
			fmt.Println(bar, h.TotalCount())
			ret.Buckets = append(ret.Buckets, &proto.HdrHistogram_Bucket{
				C: bar.Count,
				V: bar.From,
			})
		}
	}
	return ret
}

func writeJSONLine(w io.Writer, m pbproto.Message) error {
	marshaller := protojson.MarshalOptions{
		Multiline:       false,
		EmitUnpopulated: true,
		UseEnumNumbers:  false,
	}
	b, err := marshaller.Marshal(m)
	if err != nil {
		return err
	}
	b = append(b, '\n')
	_, err = w.Write(b)
	return err
}
